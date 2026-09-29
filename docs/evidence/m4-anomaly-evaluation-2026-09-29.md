# Anomaly evaluation output, 2026-09-29

Unedited output of `tools/evaluate_anomalies.sh build/mt-anomaly data/` on the four Danish slices
(Danish historical AIS data, 2026-04-22). Summarised in [anomaly-evaluation.md](../anomaly-evaluation.md).

## oresund

```
rules: speed limit 50 kn, jump slack 500 m, gap 600 s when moving >= 2 kn, identity conflict after 3 reports from a second position
reports: 144414, vessels: 411, span: 2.00 h
second positions that took over after the first fell silent: 0
silences not flagged because the network was down for part of them: 0
silences not judged because they began before listening started: 0
silences not flagged because several vessels reappeared together (coverage returned): 0
silences not flagged because the vessel had not been heard regularly before: 1
reports from search-and-rescue aircraft, not checked: 0
```

Flags on the data as recorded:

| Flag | Count | Vessels flagged |
|---|---:|---:|
| impossible_speed | 0 | 0 |
| position_jump | 0 | 0 |
| gap | 5 | 4 |
| identity_conflict | 0 | 0 |

planted: 5 per kind per seed, seeds 1-20; jumps 3-30 km, speeds 55-100 kn, gaps 15-40 min, second transmitter 5+ km away

| Planted anomaly | Planted | Detected | Recall |
|---|---:|---:|---:|
| impossible_speed | 100 | 100 | 100.0% |
| position_jump | 100 | 100 | 100.0% |
| gap | 100 | 97 | 97.0% |
| identity_conflict | 100 | 98 | 98.0% |

flags caused by planting that match no planted case: 3

Each flag on the data as recorded:

```
kind,mmsi,time,lat,lon,value,distance_m
gap,219026302,1776861218,55.64746,12.55428,1006.0,0
gap,219026304,1776863003,55.64740,12.55282,865.0,100
gap,219001375,1776863108,55.67571,12.58880,3450.0,445
gap,209114000,1776865394,55.46943,12.30118,5761.0,1457
gap,219001375,1776865506,55.67289,12.58329,633.0,1105
```

## storebaelt

```
rules: speed limit 50 kn, jump slack 500 m, gap 600 s when moving >= 2 kn, identity conflict after 3 reports from a second position
reports: 68544, vessels: 133, span: 2.00 h
second positions that took over after the first fell silent: 0
silences not flagged because the network was down for part of them: 16
silences not judged because they began before listening started: 0
silences not flagged because several vessels reappeared together (coverage returned): 0
silences not flagged because the vessel had not been heard regularly before: 0
reports from search-and-rescue aircraft, not checked: 0
```

Flags on the data as recorded:

| Flag | Count | Vessels flagged |
|---|---:|---:|
| impossible_speed | 0 | 0 |
| position_jump | 1 | 1 |
| gap | 0 | 0 |
| identity_conflict | 0 | 0 |

planted: 5 per kind per seed, seeds 1-20; jumps 3-30 km, speeds 55-100 kn, gaps 15-40 min, second transmitter 5+ km away

| Planted anomaly | Planted | Detected | Recall |
|---|---:|---:|---:|
| impossible_speed | 100 | 100 | 100.0% |
| position_jump | 100 | 100 | 100.0% |
| gap | 100 | 82 | 82.0% |
| identity_conflict | 100 | 99 | 99.0% |

flags caused by planting that match no planted case: 1

Each flag on the data as recorded:

```
kind,mmsi,time,lat,lon,value,distance_m
position_jump,219015715,1776876160,55.04127,10.66956,515.2,530
```

## skagen

```
rules: speed limit 50 kn, jump slack 500 m, gap 600 s when moving >= 2 kn, identity conflict after 3 reports from a second position
reports: 181283, vessels: 224, span: 2.00 h
second positions that took over after the first fell silent: 0
silences not flagged because the network was down for part of them: 0
silences not judged because they began before listening started: 0
silences not flagged because several vessels reappeared together (coverage returned): 0
silences not flagged because the vessel had not been heard regularly before: 0
reports from search-and-rescue aircraft, not checked: 0
```

Flags on the data as recorded:

| Flag | Count | Vessels flagged |
|---|---:|---:|
| impossible_speed | 1 | 1 |
| position_jump | 1 | 1 |
| gap | 2 | 2 |
| identity_conflict | 0 | 0 |

planted: 5 per kind per seed, seeds 1-20; jumps 3-30 km, speeds 55-100 kn, gaps 15-40 min, second transmitter 5+ km away

| Planted anomaly | Planted | Detected | Recall |
|---|---:|---:|---:|
| impossible_speed | 100 | 100 | 100.0% |
| position_jump | 100 | 100 | 100.0% |
| gap | 100 | 100 | 100.0% |
| identity_conflict | 100 | 99 | 99.0% |

flags caused by planting that match no planted case: 1

Each flag on the data as recorded:

```
kind,mmsi,time,lat,lon,value,distance_m
gap,219028008,1776839438,57.66451,10.51288,883.0,904
position_jump,970010055,1776841418,57.76064,10.45133,1295.3,666
impossible_speed,219009229,1776842019,57.89538,10.28091,65.7,0
gap,219000407,1776843558,57.30010,10.91980,1631.0,5
```

## bornholm

```
rules: speed limit 50 kn, jump slack 500 m, gap 600 s when moving >= 2 kn, identity conflict after 3 reports from a second position
reports: 13982, vessels: 70, span: 2.00 h
second positions that took over after the first fell silent: 0
silences not flagged because the network was down for part of them: 0
silences not judged because they began before listening started: 0
silences not flagged because several vessels reappeared together (coverage returned): 0
silences not flagged because the vessel had not been heard regularly before: 0
reports from search-and-rescue aircraft, not checked: 0
```

Flags on the data as recorded:

| Flag | Count | Vessels flagged |
|---|---:|---:|
| impossible_speed | 0 | 0 |
| position_jump | 0 | 0 |
| gap | 0 | 0 |
| identity_conflict | 0 | 0 |

planted: 5 per kind per seed, seeds 1-20; jumps 3-30 km, speeds 55-100 kn, gaps 15-40 min, second transmitter 5+ km away

| Planted anomaly | Planted | Detected | Recall |
|---|---:|---:|---:|
| impossible_speed | 73 | 73 | 100.0% |
| position_jump | 78 | 78 | 100.0% |
| gap | 62 | 62 | 100.0% |
| identity_conflict | 59 | 59 | 100.0% |

flags caused by planting that match no planted case: 0

## Sensitivity (all four slices)

| Planted case | Planted | Detected | Recall |
|---|---:|---:|---:|
| Jump 0.5-1 km | 378 | 197 | 52.1% |
| Jump 1-2 km | 378 | 317 | 83.9% |
| Jump 2-3 km | 378 | 354 | 93.7% |
| Jump 3-30 km | 378 | 378 | 100.0% |
| Gap 5-10 min (below the 10 min rule) | 362 | 14 | 3.9% |
| Gap 10-15 min | 362 | 347 | 95.9% |
| Gap 15-40 min | 362 | 341 | 94.2% |
| Second transmitter 1-2 km away | 148 | 140 | 94.6% |
| Second transmitter 2-5 km away | 215 | 214 | 99.5% |
| Second transmitter 5+ km away | 359 | 355 | 98.9% |
| Speed 50-55 kn | 373 | 373 | 100.0% |

Regression checks:

```
ok:   oresund: 5 flags on the data as recorded (<= 10)
ok:   oresund: 3 extra flags caused by planting (<= 5)
ok:   oresund: impossible_speed recall 100.0% (>= 95%)
ok:   oresund: position_jump recall 100.0% (>= 95%)
ok:   oresund: gap recall 97.0% (>= 95%)
ok:   oresund: identity_conflict recall 98.0% (>= 95%)
ok:   storebaelt: 1 flags on the data as recorded (<= 10)
ok:   storebaelt: 1 extra flags caused by planting (<= 5)
ok:   storebaelt: impossible_speed recall 100.0% (>= 95%)
ok:   storebaelt: position_jump recall 100.0% (>= 95%)
ok:   storebaelt: gap recall 82.0% (>= 75%)
ok:   storebaelt: identity_conflict recall 99.0% (>= 95%)
ok:   skagen: 4 flags on the data as recorded (<= 10)
ok:   skagen: 1 extra flags caused by planting (<= 5)
ok:   skagen: impossible_speed recall 100.0% (>= 95%)
ok:   skagen: position_jump recall 100.0% (>= 95%)
ok:   skagen: gap recall 100.0% (>= 95%)
ok:   skagen: identity_conflict recall 99.0% (>= 95%)
ok:   bornholm: 0 flags on the data as recorded (<= 10)
ok:   bornholm: 0 extra flags caused by planting (<= 5)
ok:   bornholm: impossible_speed recall 100.0% (>= 95%)
ok:   bornholm: position_jump recall 100.0% (>= 95%)
ok:   bornholm: gap recall 100.0% (>= 95%)
ok:   bornholm: identity_conflict recall 100.0% (>= 95%)
```
