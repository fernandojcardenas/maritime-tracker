# Pair-search benchmark output, 2026-09-29

Unedited output of:

```
mt-bench --dk-csv data/oresund-2026-04-22.csv --dk-csv data/skagen-2026-04-22.csv \
         --dk-csv data/storebaelt-2026-04-22.csv --barentswatch norway-live-2026-09-29.jsonl
```

Machine: Intel(R) Xeon(R) Processor @ 2.80GHz, 2 vCPUs, g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, CMake Release (-O3).
Danish slices: Danish historical AIS data, 2026-04-22. norway-live: the recording from Live feed run #3
(BarentsWatch, NLOD licence; contains data made available by the Norwegian Coastal Administration).
Summarised in [spatial-index-benchmark.md](../spatial-index-benchmark.md).

### Real traffic

| Workload | Vessels | Pairs within 6 nm | Brute force ms (checks) | Latitude sweep ms (checks) | Grid ms (checks) | k-d tree ms (checks) |
|---|---:|---:|---:|---:|---:|---:|
| oresund-2026-04-22, busiest minute | 219 | 4,358 | 0.17 (23,871) | 0.13 (7,783) | 0.11 (8,493) | 0.24 (9,202) |
| skagen-2026-04-22, busiest minute | 159 | 2,578 | 0.05 (12,561) | 0.06 (4,329) | 0.07 (4,042) | 0.22 (4,775) |
| storebaelt-2026-04-22, busiest minute | 76 | 475 | 0.02 (2,850) | 0.02 (757) | 0.02 (838) | 0.03 (956) |
| norway-live-2026-09-29, every vessel | 3,752 | 40,838 | 16 (7,036,876) | 2.33 (134,244) | 2.21 (77,060) | 4.41 (90,993) |

### Global feed: the largest real snapshot, copied around the globe

| Workload | Vessels | Pairs within 6 nm | Brute force ms (checks) | Latitude sweep ms (checks) | Grid ms (checks) | k-d tree ms (checks) |
|---|---:|---:|---:|---:|---:|---:|
| copies of "norway-live-2026-09-29, every vessel" around the globe | 10,000 | 100,569 | 134 (49,995,000) | 5.88 (391,156) | 5.71 (193,829) | 12 (214,298) |
| copies of "norway-live-2026-09-29, every vessel" around the globe | 100,000 | 1,083,581 | not run | 109 (12,786,682) | 74 (2,108,042) | 130 (2,423,663) |
| copies of "norway-live-2026-09-29, every vessel" around the globe | 1,000,000 | 11,519,938 | not run | 4354 (767,152,465) | 1046 (22,981,273) | 1578 (27,558,134) |

### Busy port: the first snapshot given, made denser

| Workload | Vessels | Pairs within 6 nm | Brute force ms (checks) | Latitude sweep ms (checks) | Grid ms (checks) | k-d tree ms (checks) |
|---|---:|---:|---:|---:|---:|---:|
| "oresund-2026-04-22, busiest minute" made denser | 1,000 | 96,310 | 2.67 (499,500) | 4.73 (166,608) | 3.73 (183,669) | 5.69 (145,717) |
| "oresund-2026-04-22, busiest minute" made denser | 2,000 | 369,485 | 11 (1,999,000) | 22 (652,400) | 19 (713,580) | 24 (548,431) |
| "oresund-2026-04-22, busiest minute" made denser | 5,000 | 2,323,788 | 86 (12,497,500) | 170 (4,081,273) | 141 (4,487,759) | 185 (3,181,355) |

all methods returned identical pairs: yes

Where the time goes at a million vessels (`mt-bench ... --profile METHOD 1000000`):

```
latitude_sweep on 1000000 vessels: 11519938 pairs, 767152465 distance checks; convert 48.8 ms, search 3774.4 ms, sort 426.4 ms
grid on 1000000 vessels: 11519938 pairs, 22981273 distance checks; convert 39.3 ms, search 631.9 ms, sort 400.6 ms
kd_tree on 1000000 vessels: 11519938 pairs, 27558134 distance checks; convert 54.1 ms, search 1163.6 ms, sort 337.2 ms
```
