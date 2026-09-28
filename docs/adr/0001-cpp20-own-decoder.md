# ADR 0001: C++20 and a decoder written from the specification

Date: 2026-09-28
Status: Accepted

## Context

The service has to parse a continuous stream of untrusted radio data, keep
state for thousands of vessels and run the same code on a laptop and, later, a
Raspberry Pi. Mature AIS decoders exist (AIS-catcher in C++, pyais in Python,
libais), and using one would save time.

## Decision

Write the service in C++20 and implement the NMEA/AIS decoder directly from
ITU-R M.1371 and IEC 62320-1, instead of depending on an existing decoder.

- C++20 because the target is a long-running, low-latency service on small
  hardware, and C++ is the main language of the embedded and sensor systems
  this project is modelled on.
- An own decoder because parsing hostile input safely is a core part of the
  project: bounds-checked bit reads, explicit handling of every malformed
  case, error counters instead of silent drops, and fuzzing with sanitizers.
- pyais is still used, but only as an independent reference in CI
  (`tools/crosscheck.py`), so every decoded field is checked against a second
  implementation on real data.
- `std::expected` is C++23, so a small `Expected<T, E>` type is used instead.

## Consequences

- More code to own and test, which the unit tests, real-capture test,
  cross-check and fuzz targets cover.
- The decoder has no GPL dependencies, so the project can stay MIT licensed.
- Only the message types the tracker needs are fully decoded (1–3, 5, 18, 19,
  24); other types are counted by header.
