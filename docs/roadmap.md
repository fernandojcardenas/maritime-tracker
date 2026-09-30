# Roadmap

The goal is a real-time vessel tracking service that turns a raw AIS feed into
clean tracks, flags suspicious behaviour and scores collision risk. It needs no
hardware: all input comes from public AIS data. A Raspberry Pi with an RTL-SDR
receiver can be added later as one more input, producing the same NMEA
sentences.

**Status: complete (2026-09-29).** All seven milestones are done, each
verified on GitHub (CI and the evaluation workflow green, published numbers
reproduced).

| Milestone | What it adds | Status |
|---|---|---|
| M1 Decoder | NMEA/AIVDM parser, tag blocks, multi-fragment reassembly, message types 1–3, 5, 18, 19, 24; unit tests, a recorded-traffic test, a cross-check against an independent decoder, two fuzz targets | Done |
| M2 Ingest | Live TCP client for the Norwegian open feed with reconnect and backpressure; replay of recorded files at real or accelerated speed; hourly recording | Done: one hour of live BarentsWatch data, 132,270 records, 0 lost ([log](evidence/m2-live-run-2026-09-29.txt)); the raw TCP feed refused US networks |
| M3 Tracker | Per-vessel Kalman filter (constant-velocity model) fusing position with speed and course, NIS gating, restarts, expiry; evaluated on held-out real traffic against two baselines | Done ([evaluation](tracker-evaluation.md)); long silences move to M4 |
| M4 Anomalies | Position jumps, impossible speed, reporting gaps, identity conflicts (one MMSI in two places); scored against labelled cases from Danish historical data | Done ([evaluation](anomaly-evaluation.md)): 97–100% of clear-cut planted cases found on four slices, 10 flags in 8 hours of real traffic; runs live in `mt-ingest` |
| M5 Collision risk | Closest point of approach (CPA/TCPA) for nearby pairs; encounter classification under COLREGs rules 13–15 (overtaking, head-on, crossing) | Done ([evaluation](collision-risk-evaluation.md)): roles checked against what vessels did; overtaking and open-water commercial crossings match the rules; runs live in `mt-ingest` |
| M6 Spatial index | Uniform grid vs k-d tree for neighbour queries, with benchmarks and profiler output | Done ([benchmark](spatial-index-benchmark.md)): grid chosen, 4x faster than the latitude sweep at a million vessels; profiling found the output sort as the largest cost |
| M7 API and map | WebSocket API and a browser map; Docker image; recorded demo from a real session | Done ([live map](live-map.md)): own HTTP/WebSocket server, snapshot then deltas (about 11 KB a second at real time), offline coastline; the Docker image replays a sample or goes live; demo recorded from the live hour |

## Beyond M7 (optional)

Not planned for a date; ideas in the order they would add the most:

- Vessel names and ship types on the map (static messages are decoded but
  not yet joined to tracks).
- Decode AIS directly from public IQ recordings (signal processing).
- An RTL-SDR receiver as a live input, producing the same NMEA sentences.
