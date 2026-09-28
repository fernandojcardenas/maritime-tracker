# maritime-tracker

[![CI](https://github.com/fernandojcardenas/maritime-tracker/actions/workflows/ci.yml/badge.svg)](https://github.com/fernandojcardenas/maritime-tracker/actions/workflows/ci.yml)

A real-time vessel tracking service in C++20. It turns a raw AIS feed (the
radio messages ships broadcast about their position, course and identity) into
clean tracks, flags suspicious behaviour and scores collision risk.

It runs entirely on public data: no radio hardware is needed. A Raspberry Pi
with an RTL-SDR receiver can be added later as one more input.

**Status:** milestone 1 of 7, the decoder. See the [roadmap](docs/roadmap.md).

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

## How it's tested

| Check | What it proves | Where |
|---|---|---|
| 40 unit tests (GoogleTest) | Every parser error path, bit-level field decoding, fragment reassembly and eviction | `tests/` |
| Recorded-traffic test | Exact counts on 85,194 lines of real AIS, including its malformed sentences | `tests/capture_test.cpp` |
| Cross-check against pyais | Every decoded field of 76,130 messages matches an independent open-source decoder | `tools/crosscheck.py` |
| ASan + UBSan | No memory errors or undefined behaviour, with GCC and Clang | CI `test` job |
| Two libFuzzer targets | Arbitrary bytes into the stream decoder and into the message decoders | `fuzz/`, CI `fuzz` job |
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
src/                parser, bit reader, message decoders, stream decoder
apps/mt-decode/     command-line decoder
tests/              unit tests and the recorded-traffic test
fuzz/               libFuzzer targets
tools/              cross-check against pyais
testdata/           recorded AIS traffic (MIT, from pyais)
docs/               roadmap, data sources, architecture decisions
```

## Data and scope

All data is public: see [data sources](docs/data-sources.md) and
[testdata](testdata/README.md). Nothing here uses government or military
systems or data. Design decisions are recorded in [docs/adr](docs/adr).

## Licence

MIT. See [LICENSE](LICENSE). The test data is MIT licensed by its authors; see
[testdata/LICENSE.pyais](testdata/LICENSE.pyais).
