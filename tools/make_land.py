#!/usr/bin/env python3
"""Builds web/land.json, the live map's coastline, from Natural Earth.

Natural Earth land polygons (public domain, naturalearthdata.com), from the
natural-earth-vector repository at a fixed commit, checked by SHA-256:

  - 1:10m (about 200 m) inside DETAIL_BOX, the Nordic waters where the data
    in this repository comes from, so fjords and sounds are open water;
  - 1:50m (about 1-2 km) everywhere else, to keep the file small.

Coordinates are rounded to 0.001 degree (about 100 m) and properties dropped.
Needs shapely (pip install shapely).

    tools/make_land.py web/land.json
"""
import hashlib
import json
import sys
import urllib.request

from shapely.geometry import box, mapping, shape
from shapely.ops import unary_union

BASE = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/ca96624a56bd078437bca8184e78163e5039ad19/geojson/"
SOURCES = {
    "ne_50m_land.geojson": "e874b27a51d146452be360cafb3cc50c86001074a67d534113e6534682f9826b",
    "ne_10m_land.geojson": "1ac90796408bc6ad6911d69448485d3c4dbf2190370080368a09976e1c9f7416",
}
DETAIL_BOX = box(0.0, 53.5, 35.0, 81.5)  # lon/lat: Denmark, Norway, Sweden, Svalbard


def fetch(name):
    raw = urllib.request.urlopen(BASE + name, timeout=120).read()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != SOURCES[name]:
        sys.exit(f"{name}: checksum mismatch {digest}")
    return [shape(f["geometry"]) for f in json.loads(raw)["features"]]


def rounded(coords):
    if isinstance(coords[0], (int, float)):
        return [round(coords[0], 3), round(coords[1], 3)]
    return [rounded(c) for c in coords]


def polygons(geom):
    if geom.is_empty:
        return []
    if geom.geom_type == "Polygon":
        return [geom]
    return [g for g in getattr(geom, "geoms", []) if g.geom_type == "Polygon"]


def main(out):
    coarse = fetch("ne_50m_land.geojson")
    fine = fetch("ne_10m_land.geojson")
    outside = [g.difference(DETAIL_BOX) for g in coarse]
    inside = unary_union([g.intersection(DETAIL_BOX) for g in fine if g.intersects(DETAIL_BOX)])
    parts = [p for g in outside for p in polygons(g)] + polygons(inside)
    features = [{"type": "Feature", "properties": {},
                 "geometry": {"type": "Polygon", "coordinates": rounded(mapping(p)["coordinates"])}}
                for p in parts]
    with open(out, "w") as fh:
        json.dump({"type": "FeatureCollection", "features": features}, fh, separators=(",", ":"))
    print(f"{out}: {len(features)} polygons")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "web/land.json")
