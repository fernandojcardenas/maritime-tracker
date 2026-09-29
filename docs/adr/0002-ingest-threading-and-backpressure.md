# ADR 0002: One reader thread, a drop-oldest queue, and reconnect forever

Date: 2026-09-28
Status: Accepted

## Context

A live AIS feed is an unbounded stream from a server we don't control. It can
stall, drop the connection, or send a burst faster than we decode. Later
milestones add a tracker and anomaly checks, so decoding will get slower, not
faster.

## Decision

- **Two threads.** A reader thread owns the socket (or the replay file) and
  does nothing but frame lines. The main thread decodes. They are joined by a
  fixed-capacity queue.
- **Drop oldest when full.** If the decoder falls behind, `push()` discards the
  oldest line and counts it. For live traffic, the newest position of a
  vessel is worth more than an old one, and a reader that blocked would let
  the TCP window fill until the server stalls or disconnects us.
- **Reconnect forever, with backoff and jitter.** Connect failures, peer
  closes, read errors and silent connections (no bytes for 30 s) all end the
  connection. The client waits a random time between half and all of the
  current backoff (starting at 0.5 s, doubling to a 30 s cap) and reconnects.
  A connection that delivered data resets the backoff.
- **Everything bounded and counted.** Lines over 1,024 bytes are dropped by
  the framer; every drop, timeout and reconnect has its own counter in the
  statistics line.
- **Injected time for replay.** Replay pacing takes a `Clock` interface, so
  tests use a fake clock and run in milliseconds.

## Consequences

- Under sustained overload, data is lost, visibly, in `queue-dropped`, never
  silently and never by blocking the network.
- POSIX sockets only (Linux and macOS); no Windows support.
- ThreadSanitizer runs in CI to keep the two-thread design race-free.

## Addendum, 2026-09-29: a second live source over HTTPS

The raw NMEA feed refused connections from two US networks, so live runs now
default to the BarentsWatch Live AIS API (same Norwegian data, HTTPS, one JSON
record per line). The HTTPS part is delegated to `curl` in
`tools/barentswatch_stream.sh`, which handles the OAuth token and reconnects;
`mt-ingest --stdin` reads the pipe through the same bounded line framer and
queue. This keeps TLS and credential handling out of the C++ service, at the
cost of one extra process. The JSON parser is nlohmann/json (pinned by
SHA-256), fuzzed like the other input parsers.
