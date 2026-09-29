# ADR 0006: A uniform grid in Earth-centred coordinates for pair search

Date: 2026-09-29
Status: Accepted

## Context

Collision risk (M5) needs, every minute, every pair of vessels under way
within 6 nm of each other. M5 shipped with a sweep over latitude: sort by
latitude, compare only vessels within 6 nm north-south. That is exact but
compares every vessel with every other vessel in the same latitude band, all
the way round the Earth. A worldwide feed carries a few hundred thousand
vessels at once.

## Decision

- **Measure four methods on real and scaled traffic before choosing:** brute
  force (the reference), the latitude sweep, a uniform grid and a k-d tree
  ([spatial-index-benchmark.md](../spatial-index-benchmark.md)).
- **All four work on the same exact test,** the straight-line distance
  through the Earth between Earth-centred coordinates, which is shorter than
  a threshold exactly when the great-circle distance is. They return
  identical pairs; tests, a fuzz target and every benchmark run check it.
- **Grid and tree index Earth-centred 3D coordinates,** not latitude and
  longitude, so the date line and the poles need no special cases.
- **The grid is the default for `find_encounters`.** Cubes as large as the
  search radius; each occupied cube is compared with itself and with its
  neighbours that have a larger key, so each pair of cubes is visited once.
- **Output is sorted by bucketing on the first index,** O(P + n) instead of
  O(P log P) on P pairs. Profiling showed the general sort was the largest
  single cost at scale.

## Alternatives considered

- **k-d tree as default.** Close to the grid on distance checks, but slower
  at every size measured: per-query traversal costs more than hashing into
  a fixed neighbourhood, and in sparse traffic its eight-point leaves cover
  wide areas.
- **Keep the latitude sweep.** As fast as the grid up to about 10,000
  vessels, four times slower at a million: vessels on the same latitude
  anywhere on Earth are candidates.
- **Brute force below a size threshold.** As fast as any index under a few
  hundred vessels and fastest in very dense clusters, but the saving is
  fractions of a millisecond at real sizes; one method everywhere is
  simpler.
- **Incremental index updates** instead of a rebuild each minute. Building
  the grid is a small part of the cost (see the profile); not worth the
  complexity yet.
- **Storing points in cube order for memory locality.** Tried; no
  measurable difference; reverted.

## Consequences

- At today's feed sizes (under 4,000 vessels at once) the choice changes
  little: every method takes a few milliseconds a minute. The grid matters at
  global scale.
- Past the index, time is dominated by producing and ordering the pairs
  themselves, which grows with the number of close pairs, not the number of
  vessels. Denser traffic costs more whatever the index.
