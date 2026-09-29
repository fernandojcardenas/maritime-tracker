// Live map for maritime-tracker. Connects to the server's WebSocket, draws
// every tracked vessel (with a five-minute course line when under way), the
// pairs at risk of collision, and recent anomaly flags. No build step, no
// framework: Leaflet for the map, the DOM for the panel.
(function () {
  'use strict';

  var css = getComputedStyle(document.documentElement);
  var color = function (name) { return css.getPropertyValue(name).trim(); };
  var C = {
    moving: color('--moving'), still: color('--still'), risk: color('--risk'), flag: color('--flag'),
    land: color('--land'), coast: color('--coast')
  };
  var KNOT = 1852 / 3600;       // m/s
  var FLAG_S = 600;             // a flagged vessel stays red for 10 minutes of data time
  var COURSE_S = 300;           // course lines show 5 minutes ahead

  var map = L.map('map', { preferCanvas: true, worldCopyJump: true, attributionControl: false })
    .setView([64, 10], 4);

  // A view in the address, "#lat,lon,zoom", opens there and follows the map,
  // so a view can be shared as a link.
  var fitted = false;
  var hash = /^#(-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?),(\d+)$/.exec(location.hash);
  if (hash) {
    map.setView([+hash[1], +hash[2]], +hash[3]);
    fitted = true;
  }
  map.on('moveend', function () {
    var c = map.getCenter();
    history.replaceState(null, '', '#' + c.lat.toFixed(3) + ',' + c.lng.toFixed(3) + ',' + map.getZoom());
  });
  var renderer = L.canvas({ padding: 0.3 });

  // Coastline (Natural Earth, served by the tracker itself: no tile server needed).
  fetch('land.json').then(function (r) { return r.json(); }).then(function (geo) {
    L.geoJSON(geo, {
      interactive: false, renderer: renderer,
      style: { color: C.coast, weight: 0.7, fillColor: C.land, fillOpacity: 1 }
    }).addTo(map).bringToBack();
  });

  var vessels = new Map();       // mmsi -> {row, marker, course}
  var flagged = new Map();       // mmsi -> data time of its latest anomaly
  var atRisk = new Set();        // mmsi in an encounter now
  var anomalies = [];            // newest last
  var encounters = [];
  var encounterLayer = L.layerGroup().addTo(map);
  var dataTime = 0;

  function el(id) { return document.getElementById(id); }
  function fmt(n) { return Number(n).toLocaleString('en-US'); }
  function utc(t) { return new Date(t * 1000).toISOString().replace('T', ' ').slice(0, 19) + ' UTC'; }

  function styleFor(row) {
    var mmsi = row[0];
    var sog = row[3];
    if (flagged.has(mmsi) && dataTime - flagged.get(mmsi) < FLAG_S) return { c: C.flag, r: 5 };
    if (atRisk.has(mmsi)) return { c: C.risk, r: 5 };
    return sog >= 2 ? { c: C.moving, r: 3 } : { c: C.still, r: 2.5 };
  }

  // Where the vessel will be in `s` seconds at its current course and speed.
  function ahead(row, s) {
    var d = row[3] * KNOT * s;
    var rad = row[4] * Math.PI / 180;
    var lat = row[1] + (d * Math.cos(rad)) / 111320;
    var lon = row[2] + (d * Math.sin(rad)) / (111320 * Math.max(Math.cos(row[1] * Math.PI / 180), 0.01));
    return [lat, lon];
  }

  function tooltip(row) {
    return 'MMSI ' + row[0] + ' · ' + row[3].toFixed(1) + ' kn · ' + Math.round(row[4]) + '°';
  }

  function draw(row) {
    var v = vessels.get(row[0]);
    var s = styleFor(row);
    var pos = [row[1], row[2]];
    if (!v) {
      v = { row: row };
      v.marker = L.circleMarker(pos, {
        renderer: renderer, radius: s.r, weight: 1, color: s.c, fillColor: s.c, fillOpacity: 0.9
      }).addTo(map);
      v.marker.bindTooltip(function () { return tooltip(v.row); }, { direction: 'top' });
      vessels.set(row[0], v);
    } else {
      v.row = row;
      v.marker.setLatLng(pos);
      v.marker.setStyle({ color: s.c, fillColor: s.c });
      v.marker.setRadius(s.r);
    }
    if (row[3] >= 2) {
      var line = [pos, ahead(row, COURSE_S)];
      if (v.course) {
        v.course.setLatLngs(line);
        v.course.setStyle({ color: s.c });
      } else {
        v.course = L.polyline(line, { renderer: renderer, weight: 1, color: s.c, opacity: 0.7, interactive: false })
          .addTo(map);
      }
    } else if (v.course) {
      map.removeLayer(v.course);
      v.course = null;
    }
  }

  function remove(mmsi) {
    var v = vessels.get(mmsi);
    if (!v) return;
    map.removeLayer(v.marker);
    if (v.course) map.removeLayer(v.course);
    vessels.delete(mmsi);
  }

  function restyleAll() { vessels.forEach(function (v) { draw(v.row); }); }

  function drawEncounters() {
    encounterLayer.clearLayers();
    encounters.forEach(function (e) {
      var a = vessels.get(e.a);
      var b = vessels.get(e.b);
      if (!a || !b) return;
      L.polyline([[a.row[1], a.row[2]], [b.row[1], b.row[2]]], {
        renderer: renderer, color: C.risk, weight: 2, dashArray: '4 4', interactive: false
      }).addTo(encounterLayer);
    });
  }

  var KIND = { gap: 'Gap', position_jump: 'Position jump', impossible_speed: 'Impossible speed',
               identity_conflict: 'Identity conflict' };
  function anomalyDetail(a) {
    if (a.kind === 'gap') return Math.round(a.value / 60) + ' min silent';
    if (a.kind === 'impossible_speed') return a.value.toFixed(1) + ' kn reported';
    if (a.kind === 'position_jump') return Math.round(a.value) + ' kn implied';
    return (a.value / 1852).toFixed(1) + ' nm apart';
  }
  var TYPE = { crossing: 'Crossing', overtaking: 'Overtaking', head_on: 'Head-on', unclear: 'Unclear' };
  function encounterDetail(e) {
    var give = [];
    if (e.role_a === 'give_way') give.push(e.a);
    if (e.role_b === 'give_way') give.push(e.b);
    var who = give.length === 2 ? 'both give way' : give[0] + ' gives way';
    return who + ' · CPA ' + (e.dcpa / 1852).toFixed(2) + ' nm in ' + Math.max(1, Math.round(e.tcpa / 60)) + ' min';
  }

  function item(list, what, detail, onClick) {
    var li = document.createElement('li');
    li.tabIndex = 0;
    var w = document.createElement('div');
    w.className = 'what';
    w.textContent = what;
    var d = document.createElement('div');
    d.className = 'detail';
    d.textContent = detail;
    li.appendChild(w);
    li.appendChild(d);
    li.addEventListener('click', onClick);
    li.addEventListener('keydown', function (ev) { if (ev.key === 'Enter') onClick(); });
    list.appendChild(li);
  }
  function empty(list, text) {
    var li = document.createElement('li');
    li.className = 'empty';
    li.textContent = text;
    list.appendChild(li);
  }

  function renderPanel(totals) {
    el('clock').textContent = dataTime ? utc(dataTime) : '–';
    if (totals) {
      el('t-tracks').textContent = fmt(totals.tracks);
      el('t-messages').textContent = fmt(totals.messages);
      el('t-anomalies').textContent = fmt(totals.anomalies);
      el('t-encounters').textContent = fmt(totals.encounters);
    }
    var el_e = el('encounters');
    el_e.replaceChildren();
    el('n-encounters').textContent = '(' + encounters.length + ')';
    encounters.slice().sort(function (x, y) { return x.tcpa - y.tcpa; }).slice(0, 50).forEach(function (e) {
      item(el_e, (TYPE[e.type] || e.type) + ' · ' + e.a + ' / ' + e.b, encounterDetail(e), function () {
        var a = vessels.get(e.a);
        var b = vessels.get(e.b);
        if (a && b) map.fitBounds([[a.row[1], a.row[2]], [b.row[1], b.row[2]]], { padding: [80, 80], maxZoom: 11 });
      });
    });
    if (!encounters.length) empty(el_e, 'None');
    var el_a = el('anomalies');
    el_a.replaceChildren();
    el('n-anomalies').textContent = '(' + anomalies.length + ')';
    anomalies.slice(-50).reverse().forEach(function (a) {
      item(el_a, (KIND[a.kind] || a.kind) + ' · ' + a.mmsi,
        anomalyDetail(a) + ' · ' + utc(a.t).slice(11, 16), function () {
          map.setView([a.lat, a.lon], Math.max(map.getZoom(), 10));
        });
    });
    if (!anomalies.length) empty(el_a, 'None');
  }

  function onSnapshot(m) {
    vessels.forEach(function (v, mmsi) { remove(mmsi); });
    dataTime = m.t;
    anomalies = m.anomalies;
    flagged.clear();
    anomalies.forEach(function (a) { flagged.set(a.mmsi, a.t); });
    encounters = m.encounters;
    atRisk = new Set();
    encounters.forEach(function (e) { atRisk.add(e.a); atRisk.add(e.b); });
    m.tracks.forEach(draw);
    drawEncounters();
    renderPanel(m.totals);
    if (!fitted && m.tracks.length) {
      fitted = true;
      // Fit the middle 95% of vessels, so a few far-off ones don't shrink the view;
      // leave room for the panel on wide screens.
      var q = function (xs, f) { xs.sort(function (x, y) { return x - y; }); return xs[Math.floor(f * (xs.length - 1))]; };
      var lats = m.tracks.map(function (r) { return r[1]; });
      var lons = m.tracks.map(function (r) { return r[2]; });
      var b = L.latLngBounds([q(lats.slice(), 0.025), q(lons.slice(), 0.025)], [q(lats, 0.975), q(lons, 0.975)]);
      var wide = window.innerWidth > 700;
      map.fitBounds(b, {
        paddingTopLeft: [20, 20],
        paddingBottomRight: wide ? [340, 20] : [20, Math.round(window.innerHeight * 0.42)],
        maxZoom: 9
      });
    }
  }

  function onUpdate(m) {
    dataTime = m.t;
    m.anomalies.forEach(function (a) { anomalies.push(a); flagged.set(a.mmsi, a.t); });
    if (anomalies.length > 200) anomalies = anomalies.slice(-200);
    encounters = m.encounters;
    var risk = new Set();
    encounters.forEach(function (e) { risk.add(e.a); risk.add(e.b); });
    var changedRisk = risk.size !== atRisk.size || Array.from(risk).some(function (x) { return !atRisk.has(x); });
    atRisk = risk;
    m.removed.forEach(remove);
    m.tracks.forEach(draw);
    if (changedRisk || m.anomalies.length) restyleAll();
    drawEncounters();
    renderPanel(m.totals);
  }

  var backoff = 1000;
  function connect() {
    var ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
    var status = el('status');
    ws.onopen = function () {
      backoff = 1000;
      status.textContent = 'live';
      status.className = 'status live';
    };
    ws.onmessage = function (ev) {
      var m = JSON.parse(ev.data);
      if (m.type === 'snapshot') onSnapshot(m);
      else if (m.type === 'update') onUpdate(m);
    };
    ws.onclose = function () {
      status.textContent = 'connection lost, retrying…';
      status.className = 'status lost';
      setTimeout(connect, backoff);
      backoff = Math.min(backoff * 2, 10000);
    };
  }
  connect();
}());
