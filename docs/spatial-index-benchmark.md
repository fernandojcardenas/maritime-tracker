# Pair-search benchmark

Collision risk needs every pair of vessels within 6 nm of each other, every
minute. Which way of finding them is fastest, on real traffic and at the
scale of a worldwide feed, and why? Every number below comes from `mt-bench`;
the unedited output is in
[evidence/m6-spatial-benchmark-2026-09-29.md](evidence/m6-spatial-benchmark-2026-09-29.md)
and the profile in [evidence/m6-callgrind-grid-100k.txt](evidence/m6-callgrind-grid-100k.txt).
The decision is in [ADR 0006](adr/0006-spatial-index.md).

## Methods

| Method | How |
|---|---|
| Brute force | Every pair; the reference |
| Latitude sweep | Sort by latitude; compare vessels within 6 nm north-south (what M5 shipped with) |
| Grid | Hash vessels into cubes 6 nm wide in Earth-centred 3D coordinates; compare each cube with its 26 neighbours |
| k-d tree | A static 3D k-d tree (leaves of 8), one radius query per vessel |

All four apply the same exact test, the straight-line distance through the
Earth, and return **identical pairs**. Tests check this on clustered traffic,
several radii, the date line, the poles, duplicates and both sides of the
radius; a fuzz target checks it against brute force on arbitrary input; and
`mt-bench` fails if any method disagrees on any workload. Switching collision
risk from the sweep to the grid left every collision-risk result and all
2,020 live encounters unchanged.

## Setup

- **Real traffic:** the busiest minute of three Danish slices (every vessel
  that reported in that minute) and every vessel in the one-hour Norwegian
  live recording at once (3,752).
- **Global feed:** copies of the Norwegian snapshot rotated to random places
  on the globe (a rotation keeps all distances), up to a million vessels:
  worldwide coverage at the density of a real coast. Synthetic.
- **Busy port:** the Øresund minute made denser, up to 5,000 vessels. Synthetic.
- **Measurement:** median of at least 3 runs; each run converts positions,
  builds the index, finds and sorts the pairs. One thread; a 2-vCPU cloud
  machine (Xeon, 2.8 GHz), GCC 13, `-O3`. Times vary by about 20% from run
  to run; distance checks (candidate pairs whose distance was computed) are
  exact and do not depend on the machine.

## Results

Milliseconds (distance checks):

| Workload | Vessels | Pairs | Brute force | Latitude sweep | Grid | k-d tree |
|---|---:|---:|---:|---:|---:|---:|
| Øresund, busiest minute | 219 | 4,358 | 0.17 (23,871) | 0.13 (7,783) | 0.11 (8,493) | 0.24 (9,202) |
| Norway, every vessel | 3,752 | 40,838 | 16 (7.0 M) | 2.3 (134 k) | 2.2 (77 k) | 4.4 (91 k) |
| Global feed | 10,000 | 100,569 | 134 (50 M) | 5.9 (391 k) | 5.7 (194 k) | 12 (214 k) |
| Global feed | 100,000 | 1.08 M | not run | 109 (12.8 M) | 74 (2.1 M) | 130 (2.4 M) |
| Global feed | 1,000,000 | 11.5 M | not run | 4,354 (767 M) | **1,046** (23 M) | 1,578 (28 M) |
| Busy port | 1,000 | 96,310 | **2.7** (500 k) | 4.7 (167 k) | 3.7 (184 k) | 5.7 (146 k) |
| Busy port | 5,000 | 2.32 M | **86** (12.5 M) | 170 (4.1 M) | 141 (4.5 M) | 185 (3.2 M) |

1. **At real feed sizes the method barely matters.** The busiest Danish
   minute takes a tenth of a millisecond with any method, and a whole
   Norwegian coast at once takes 2–4 ms with any index. The live pipeline
   assesses risk once a minute.
2. **At global scale the grid wins:** 1.0 s for a million vessels, 4.2
   times faster than the sweep and 1.5 times faster than the k-d tree. The
   sweep compares every vessel with every other vessel at the same latitude
   anywhere on Earth: 767 million checks against 23 million.
3. **Checks are not time.** The grid does 33 times fewer checks than the
   sweep at a million vessels but is 4 times faster. Once the index has done
   its job, most of the time goes into producing and ordering 11.5 million
   pairs, which no index can avoid.
4. **In a dense port brute force is fastest.** When a fifth of all pairs
   really are within 6 nm, there is little to prune, and the simplest loop
   wins.
5. **The k-d tree does about as few checks as the grid but is slower
   everywhere:** walking the tree per vessel costs more than looking up 26
   neighbouring cubes, and in sparse traffic its leaves cover wide areas.

## What the profiler found

The first version of every method finished with a general sort of the pairs.
Timing each phase at a million vessels showed it:

| Grid, 1,000,000 vessels (one `--profile` run each) | Convert | Search | Sort | Total |
|---|---:|---:|---:|---:|
| General sort (`std::sort`) | 39 ms | 707 ms | 1,025 ms | 1,771 ms |
| Bucket sort by first vessel, then each small bucket | 39 ms | 632 ms | 401 ms | 1,072 ms |

Sorting was the largest single cost. Pairs come out as (i, j) with i < n, so
counting pairs per i and placing them directly is linear time, and each
bucket (one vessel's neighbours) is tiny. The change made every method
faster (benchmark medians at a million vessels: grid 1,704 → 1,046 ms,
sweep 5,378 → 4,354 ms, k-d tree 1,701 → 1,578 ms), and even the whole
Norwegian coast went from 5.1 ms to 2.2 ms with the grid.

The callgrind profile of the grid after the change (100,000 vessels, only
instructions inside the pair search) shows no single hot spot left: the
hottest line, testing a distance and recording the pair, is 7% of
instructions; the bucket sort's two passes 5%; sine and cosine for
converting positions 8%.

One idea did not work: copying points into cube order, so each cube's points
sit together in memory, made no measurable difference (search 562–666 ms
over three runs against 629 ms before; within run-to-run variation), and was reverted.

## Limitations

- The scaled workloads are synthetic: rotated copies of one real coast, and a
  real port made denser. A real worldwide feed mixes empty oceans with ports
  far denser than any copy of Norway.
- One thread. The grid's cubes would split naturally across threads.
- The index is rebuilt every minute; memory use was not measured.
- A shared 2-vCPU machine: compare methods within a run, not absolute times
  across machines.

## Reproduce

```sh
tools/fetch_dk_slice.sh data/oresund-2026-04-22.csv data/
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target mt-bench
build/mt-bench --dk-csv data/oresund-2026-04-22.csv --dk-csv data/skagen-2026-04-22.csv \
               --dk-csv data/storebaelt-2026-04-22.csv --barentswatch norway-live-2026-09-29.jsonl
build/mt-bench --barentswatch norway-live-2026-09-29.jsonl --profile grid 1000000   # phase times
```

CI runs a quick version on the live-data test slice (up to 100,000 vessels)
on every push and fails if any method disagrees.
