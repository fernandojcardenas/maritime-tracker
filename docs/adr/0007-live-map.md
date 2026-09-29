# ADR 0007: A built-in WebSocket server and an offline map

Date: 2026-09-29
Status: Accepted

## Context

M7 puts the tracker's output in front of a person: every vessel, the pairs
at risk and the anomaly flags, live, in a browser. It has to run as one
process next to the tracker, on a laptop or in a container, with thousands
of vessels changing every second, and it must not be able to slow the
tracker down. It will also accept connections from browsers, so it is new
attack surface in a project whose inputs are otherwise files and one feed.

## Decision

- **Write the HTTP/1.1 and WebSocket server in the project** (about 670
  lines of C++, SHA-1 and base64 included), instead of adding a networking library.
  The server needs a small part of either protocol: GET for a fixed set of
  files, one upgrade, server-to-client text frames, pings and close. That
  part is small enough to write, test against the published vectors and
  fuzz; a library would bring TLS, routing and compression that are not
  used, and a dependency to keep current.
- **One thread, `poll()`, non-blocking sockets, one queue per client.** The
  tracker calls `publish()` once a second; it formats the message once and
  hands the same shared buffer to every client. A client more than 16 MiB
  behind is disconnected, so a stalled browser costs memory up to that bound
  and never costs the tracker or other clients time.
- **Snapshot, then deltas.** A new client gets everything once; after that,
  only tracks with a newer report, expired MMSIs, new anomalies and the
  current pairs at risk. At real time on the Norwegian feed that is about
  11 KB a second.
- **Compact rows, plain JSON.** Tracks are arrays, not objects, to keep
  messages small, and the format stays readable in the browser's network
  panel. No binary format until size is a measured problem.
- **Everything built into the binary.** `web/` (page, script, style, Leaflet
  and the coastline) becomes a C++ source at build time; a test checks it
  matches the files byte for byte. The binary is the deployment.
- **An offline coastline instead of map tiles.** Natural Earth land polygons,
  1:10m in Nordic waters and 1:50m elsewhere (1.7 MB), served by the
  tracker. No tile server, no API key, no third-party requests from the
  viewer's browser, and it works without internet.
- **Safe defaults.** Bound to 127.0.0.1 unless told otherwise; limits on
  clients, request size, request time and client message size; a
  Content-Security-Policy that allows only the server itself; no
  authentication or TLS, which belong to a reverse proxy in front.
- **A Docker image that replays the bundled sample by default** and goes live
  when BarentsWatch credentials are given in the environment, so anyone can
  see the map with one command and no account.

## Alternatives considered

- **A library (Boost.Beast, uWebSockets, libwebsockets).** Well tested, but
  each is larger than the whole feature and several bring their own event
  loop. Revisit if the server needs TLS or many thousands of clients.
- **Server-sent events** instead of WebSocket. Simpler (plain HTTP), and the
  traffic is one-way. WebSocket was chosen because the next step, a client
  asking for a region or a vessel's history, needs the other direction; and
  the protocol work is the part worth showing.
- **Sending every track every second.** Simplest client, but ten times the
  traffic at real time (128 KB instead of 11 KB).
- **A tile map (OpenStreetMap tiles).** Much richer, but every viewer would
  request tiles from a third party, the OpenStreetMap tile usage policy restricts
  heavy use, and it breaks offline. The coastline is enough to read traffic.
- **A frontend framework and a build step.** Not needed for one page; plain
  JavaScript and Leaflet keep `web/` readable and the build C++-only.

## Consequences

- The server is tested at three levels: unit tests with published vectors,
  socket tests against the real server (including a client that stops
  reading, run 20 times under ThreadSanitizer), and a fuzz target. The CI
  docker job checks the running image from outside.
- The map shows only what the tracker holds; names and ship types need
  static messages joined to tracks (not done yet).
- Changing a web file means rebuilding the binary. For development, the
  test that compares the built-in files with `web/` catches a stale build.
