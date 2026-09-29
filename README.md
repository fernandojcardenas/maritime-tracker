# maritime-tracker

[![CI](https://github.com/fernandojcardenas/maritime-tracker/actions/workflows/ci.yml/badge.svg)](https://github.com/fernandojcardenas/maritime-tracker/actions/workflows/ci.yml)

A real-time vessel tracking service in C++20. It turns a raw AIS feed (the
radio messages ships broadcast about their position, course and identity) into
clean tracks, flags suspicious behaviour and scores collision risk.

It runs entirely on public data: no radio hardware is needed. A Raspberry Pi
with an RTL-SDR receiver can be added later as one more input.

**Status:** milestones 1 (decoder), 2 (live ingest and replay) and 3 (tracker) done. See the [roadmap](docs/roadmap.md).

## Why

I'm a U.S. Coast Guard electronics technician, and marine radio and
navigation are the domain I know best. This project takes that domain into
software: parse untrusted radio data safely, fuse position reports into
tracks, and reason about traffic the way a watchstander does, including which
vessel has to give way.

## What works today (M1: decoder)

- NMEA 0183 parser for `!xxVDM` / `!xxVDO` sentences with checksum
  validation and IEC 62320-1 tag blocks (receiving station, timestamp).
- Reassembly of multi-sentence messages, with a bounded buffer so hostile or
  lossy input can't grow memory without limit.
- Full decoding of message types 1, 2, 3 (Class A position), 5 (static and
  voyage data), 18, 19 (Class B position) and 24 (Class B static data). Other
  valid types are counted by header; undefined message ids are rejected.
- "Not available" sentinel values become empty optionals, and out-of-range
  values are rejected, so later stages never see latitude 91.
- Every rejected line is counted by reason instead of dropped silently.
- `mt-decode`, a command-line tool: raw NMEA in, one JSON object per message
  out, statistics on stderr.

```
$ ./build/mt-decode < testdata/pyais-nmea-sample.nmea > messages.jsonl
lines            85194
sentences ok     85186
messages         82748
unique MMSIs     11075
fragments dropped 4
decode too short 0
decode invalid   0
unknown type     2
parse error bad_fill_bits: 8
by type: 1=61374 2=1973 3=8785 4=4043 5=2221 6=435 8=1082 9=27 10=7 11=28 16=1 17=314 18=1361 19=109 20=276 21=380 22=23 24=307 25=2
```

## Ingest and replay (M2)

`mt-ingest` reads a live feed or a recorded file, decodes it, tracks vessels,
and can record what it receives for later replay.

**One hour of live data** (2026-09-29, 13:39–14:39 UTC, BarentsWatch Live AIS
API, run on GitHub Actions): 132,270 records from 3,752 vessels along the
Norwegian coast, the Barents Sea and Svalbard, about 37 per second, with 0
invalid records and 0 dropped. The tracker ran live on the stream: 4,064 tracks,
127,927 updates, 279 gate rejections. Replaying the recording gives identical
output every time and reproduces those tracker numbers exactly
([run log](docs/evidence/m2-live-run-2026-09-29.txt)).

That run also found a bug. Replaying the hour twice gave two different results,
because file replay was sharing the live feed's drop-oldest queue and silently
lost lines once a file outgrew it. Replay now waits for queue space instead, and
CI replays a 200,000-line file on every push to keep it that way.

```
mt-ingest --tcp 153.44.253.27:5631 --record recordings/        # raw NMEA over TCP, recorded hourly
mt-ingest --replay recordings/ais-20260928-17.nmea --speed 60  # one hour per minute
tools/barentswatch_stream.sh | mt-ingest --stdin --format barentswatch --duration 3600
```

Two live sources carry the same Norwegian open AIS data: the raw NMEA feed
over TCP, and the BarentsWatch Live AIS API over HTTPS (one JSON record per
line). The raw feed did not accept connections from US networks when tested,
so the HTTPS API is the default for live runs. A small script gets the OAuth
token, streams with `curl`, and reconnects with backoff when the stream ends
or the token expires. The C++ side reads its standard input with the same
line limits as the TCP client, so no TLS library is needed in the service.

- **Reconnects forever.** Connect failures, peer closes, read errors and
  silent connections (no bytes for 30 s) all lead to a reconnect with
  exponential backoff and random jitter, capped at 30 s. Each cause is
  counted separately.
- **Bounded everywhere.** Lines over 1,024 bytes are discarded by the framer,
  and a fixed-size queue sits between the network thread and the decoder. If
  decoding falls behind, the oldest lines are dropped and counted instead of
  stalling the socket. File replay waits for space instead, so it never loses
  a line. See [ADR 0002](docs/adr/0002-ingest-threading-and-backpressure.md).
- **Recording and replay.** Recordings rotate hourly and every line gets a
  receive timestamp. Replay paces lines by those timestamps at any speed,
  shortens long outages, and never rewinds on out-of-order timestamps. Time is
  injected, so the pacing tests run instantly.

The same checks run against [`tools/fake_barentswatch.py`](tools/fake_barentswatch.py),
a local stand-in for the API: 8,366 records over three token-and-stream cycles,
0 invalid, 0 dropped, identical replays, and the secret in no log
([output](docs/evidence/m2-barentswatch-fake-run.txt)). CI repeats that test
on every push.

A local run against [`tools/fake_feed.py`](tools/fake_feed.py), which serves
recorded traffic in split chunks and drops the connection every 3,034 lines:
all 9,102 lines arrived across 3 connections, 0 were dropped, and replaying the
recording produced exactly the same decoded messages as decoding the source
file directly. Full output: [docs/evidence/m2-fake-feed-run.txt](docs/evidence/m2-fake-feed-run.txt).

## Tracker (M3)

Each vessel gets a constant-velocity Kalman filter that fuses reported
position with reported speed and course, and gates out reports that are
statistically implausible for the track ([ADR 0003](docs/adr/0003-tracker-design.md)).

It was evaluated on 144,414 real reports from 411 vessels in the Øresund
strait (Danish AIS data, 2026-04-22), tuned on one hour and tested on the
next. Median / 90th percentile error predicting a moving vessel's next
position 10–30 s ahead:

| Input | Hold last position | Dead reckoning | Kalman filter |
|---|---:|---:|---:|
| Clean | 53 / 83 m | **2 / 12 m** | 3 / 12 m |
| 15 m noise, 30% without speed/course | 54 / 93 m | 24 / 67 m | **6 / 22 m** |

On clean AIS, dead reckoning from the last report is already excellent and
the filter doesn't beat it. With degraded input the filter is about four
times more accurate. The gate also rejects about 89% of injected wild
reports. Full method, all horizons, the trade-offs and one known weakness
(long silences) are in the [tracker evaluation](docs/tracker-evaluation.md).

```
tools/fetch_dk_slice.sh data/oresund.csv          # 830 MB download, SHA-256 checked
./build/mt-track --dk-csv data/oresund.csv --eval-from 1776862800 --eval-to 1776866400 --pos-sigma 5
```

## How it's tested

| Check | What it proves | Where |
|---|---|---|
| 85 tests (GoogleTest) | Every parser error path, bit-level field decoding, fragment reassembly and eviction; line framing, queue overflow, replay pacing, recorder rotation; geodesy, filter convergence and covariance health, gating, restarts, re-anchoring; JSON records and ISO 8601 times | `tests/` |
| Tracker evaluation on real data | Held-out prediction error against two baselines, and outlier rejection; fails CI on regression | `tools/evaluate_tracker.sh`, `evaluate` workflow |
| Fake-feed TCP tests | Split lines, peer disconnect, silent connection, refused connect, prompt shutdown, against a real socket on loopback | `tests/tcp_source_test.cpp` |
| Recorded-traffic test | Exact counts on 85,194 lines of real AIS, including its malformed sentences | `tests/capture_test.cpp` |
| Cross-check against pyais | Every decoded field of 76,130 messages matches an independent open-source decoder | `tools/crosscheck.py` |
| ASan + UBSan | No memory errors or undefined behaviour, with GCC and Clang | CI `test` job |
| ThreadSanitizer | No data races in the reader thread, queue and TCP client; threaded tests repeated 20 times | CI `tsan` job |
| Five libFuzzer targets | Arbitrary bytes into the stream decoder, the message decoders, the line framer under arbitrary TCP chunking, and the parsers for downloaded CSV and streamed JSON | `fuzz/`, CI `fuzz` job |
| Live-API bridge test | Token, stream, token expiry, reconnect and clean shutdown against a fake server; the secret must not appear in any output; a 200,000-line replay must lose nothing | CI `barentswatch-bridge` job |
| Live-data test | 5,000 real records from the one-hour live run parse and track with pinned counts | `tests/barentswatch_test.cpp` |
| clang-tidy | bugprone, cert, performance, modernize and readability checks, warnings as errors | `.clang-tidy`, CI `lint` job |

The cross-check found four real problems during development, all fixed:
coordinates printed with too few digits (about 0.5 m of precision lost), names
with leading spaces, position reports rejected when a transmitter declared too
many fill bits, and undefined message ids being accepted. One difference is
intentional: this decoder rejects sentences whose fill-bit field is outside
0–5, which pyais accepts.

## Build

Requires CMake 3.24+ and a C++20 compiler (GCC 13, Clang 18 or Apple Clang).
GoogleTest is downloaded at configure time.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Options: `-DMT_SANITIZE=ON` (ASan + UBSan), `-DMT_BUILD_FUZZERS=ON` (Clang
with libFuzzer), `-DMT_WARNINGS_AS_ERRORS=ON`.

## Layout

```
include/maritime/   public headers: nmea/, ais/, util/
src/                parser, bit reader, message decoders, stream decoder, ingest (TCP, replay, recorder), tracker
apps/mt-decode/     command-line decoder
apps/mt-ingest/     live/replay ingest with recording
apps/mt-track/      tracker evaluation on recorded traffic
tests/              unit tests and the recorded-traffic test
fuzz/               libFuzzer targets
tools/              cross-check against pyais, fake TCP feed and fake live API, live stream script, data fetch, tracker evaluation
testdata/           recorded AIS traffic (MIT, from pyais)
docs/               roadmap, data sources, architecture decisions, evidence from real runs
```

## Data and scope

All data is public: see [data sources](docs/data-sources.md) and
[testdata](testdata/README.md). Nothing here uses government or military
systems or data. Design decisions are recorded in [docs/adr](docs/adr).

## Licence

MIT. See [LICENSE](LICENSE). The test data is MIT licensed by its authors; see
[testdata/LICENSE.pyais](testdata/LICENSE.pyais).
