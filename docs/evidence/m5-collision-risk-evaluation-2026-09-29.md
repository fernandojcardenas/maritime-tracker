# Collision-risk evaluation output, 2026-09-29

Unedited output of `tools/evaluate_collision_risk.sh build/mt-risk data/` on the Danish slices
(Danish historical AIS data, 2026-04-22). Summarised in [collision-risk-evaluation.md](../collision-risk-evaluation.md).

## oresund

### All vessels, CPA within 0.5 nm

```
reports: 144414, vessels: 411, assessed every 60 s; at most 168 vessels under way at once
risk: CPA within 0.50 nm within 20 min, range under 6 nm, both at 2 kn or more
pair-minutes at risk (Kalman predictor): 2156; scored against the recorded future: 1262; not scorable (reports missing): 894
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|
| 0-2 min | 478 | 41 / 217 | 41 / 213 | 26 / 147 |
| 2-5 min | 400 | 155 / 438 | 156 / 435 | 67 / 268 |
| 5-10 min | 289 | 233 / 819 | 242 / 818 | 158 / 526 |
| 10-20 min | 95 | 405 / 2669 | 409 / 2648 | 231 / 633 |

encounters: 1048 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 352

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 484 | 228 | 77 | 27, 29 of 77 (38%) | -7, 26 of 77 (34%) | 42 of 77 (55%) | 25 of 74 (34%) |
| overtaking | 238 | 42 | 17 | 859, 11 of 17 (65%) | -23, 3 of 17 (18%) | 15 of 17 (88%) | - |
| head_on | 57 | 35 | 12 | -40, 3 of 24 (12%) | - | - | - |
| unclear | 269 | 47 | 23 | -144, 13 of 46 (28%) | - | - | - |

### All vessels, CPA within 0.25 nm

```
reports: 144414, vessels: 411, assessed every 60 s; at most 168 vessels under way at once
risk: CPA within 0.25 nm within 20 min, range under 6 nm, both at 2 kn or more
pair-minutes at risk (Kalman predictor): 1294; scored against the recorded future: 783; not scorable (reports missing): 511
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|
| 0-2 min | 347 | 34 / 139 | 36 / 134 | 18 / 110 |
| 2-5 min | 237 | 121 / 313 | 115 / 309 | 56 / 217 |
| 5-10 min | 156 | 162 / 822 | 161 / 818 | 156 / 526 |
| 10-20 min | 43 | 348 / 2724 | 345 / 2709 | 296 / 716 |

encounters: 740 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 225

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 390 | 160 | 55 | 62, 22 of 55 (40%) | 71, 21 of 55 (38%) | 29 of 55 (53%) | 15 of 54 (28%) |
| overtaking | 163 | 26 | 12 | 376, 8 of 12 (67%) | -22, 2 of 12 (17%) | 12 of 12 (100%) | - |
| head_on | 65 | 33 | 14 | -43, 5 of 28 (18%) | - | - | - |
| unclear | 122 | 6 | 4 | -115, 0 of 8 (0%) | - | - | - |

### Cargo, tanker, passenger and high-speed craft only, CPA within 0.5 nm

```
reports: 144414, vessels: 411, assessed every 60 s; at most 168 vessels under way at once
risk: CPA within 0.50 nm within 20 min, range under 6 nm, both at 2 kn or more; cargo, tanker, passenger and high-speed craft only
pair-minutes at risk (Kalman predictor): 1691; scored against the recorded future: 1038; not scorable (reports missing): 653
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|
| 0-2 min | 426 | 41 / 225 | 42 / 226 | 24 / 152 |
| 2-5 min | 345 | 155 / 406 | 157 / 417 | 72 / 271 |
| 5-10 min | 208 | 224 / 665 | 234 / 664 | 148 / 484 |
| 10-20 min | 59 | 348 / 1587 | 345 / 1568 | 229 / 685 |

encounters: 787 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 204

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 352 | 132 | 52 | -5, 13 of 52 (25%) | -105, 13 of 52 (25%) | 27 of 52 (52%) | 17 of 51 (33%) |
| overtaking | 182 | 17 | 7 | -187, 3 of 7 (43%) | -515, 2 of 7 (29%) | 7 of 7 (100%) | - |
| head_on | 38 | 21 | 9 | -160, 1 of 18 (6%) | - | - | - |
| unclear | 215 | 34 | 17 | -206, 6 of 34 (18%) | - | - | - |

### Cargo, tanker, passenger and high-speed craft only, CPA within 0.25 nm

```
reports: 144414, vessels: 411, assessed every 60 s; at most 168 vessels under way at once
risk: CPA within 0.25 nm within 20 min, range under 6 nm, both at 2 kn or more; cargo, tanker, passenger and high-speed craft only
pair-minutes at risk (Kalman predictor): 1036; scored against the recorded future: 651; not scorable (reports missing): 385
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|
| 0-2 min | 311 | 33 / 144 | 36 / 137 | 17 / 113 |
| 2-5 min | 201 | 121 / 306 | 111 / 300 | 57 / 230 |
| 5-10 min | 111 | 148 / 420 | 144 / 421 | 149 / 480 |
| 10-20 min | 28 | 200 / 1640 | 205 / 1614 | 280 / 736 |

encounters: 566 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 132

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 290 | 91 | 34 | 43, 12 of 34 (35%) | 17, 11 of 34 (32%) | 19 of 34 (56%) | 11 of 33 (33%) |
| overtaking | 126 | 14 | 8 | 325, 5 of 8 (62%) | -22, 2 of 8 (25%) | 8 of 8 (100%) | - |
| head_on | 47 | 21 | 10 | -160, 1 of 20 (5%) | - | - | - |
| unclear | 103 | 6 | 4 | -115, 0 of 8 (0%) | - | - | - |

## storebaelt

### All vessels, CPA within 0.5 nm

```
reports: 68544, vessels: 132, assessed every 60 s; at most 60 vessels under way at once
risk: CPA within 0.50 nm within 20 min, range under 6 nm, both at 2 kn or more
pair-minutes at risk (Kalman predictor): 175; scored against the recorded future: 127; not scorable (reports missing): 48
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|
| 0-2 min | 33 | 39 / 305 | 49 / 360 | 16 / 116 |
| 2-5 min | 42 | 78 / 378 | 105 / 381 | 14 / 196 |
| 5-10 min | 39 | 259 / 607 | 258 / 513 | 9 / 371 |
| 10-20 min | 13 | 354 / 472 | 351 / 472 | 44 / 60 |

encounters: 41 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 21

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 16 | 10 | 7 | -20, 3 of 7 (43%) | -9, 0 of 7 (0%) | 4 of 7 (57%) | 5 of 7 (71%) |
| overtaking | 11 | 3 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| head_on | 6 | 6 | 6 | 122, 4 of 12 (33%) | - | - | - |
| unclear | 8 | 2 | 2 | -182, 1 of 4 (25%) | - | - | - |

### All vessels, CPA within 0.25 nm

```
reports: 68544, vessels: 132, assessed every 60 s; at most 60 vessels under way at once
risk: CPA within 0.25 nm within 20 min, range under 6 nm, both at 2 kn or more
pair-minutes at risk (Kalman predictor): 94; scored against the recorded future: 57; not scorable (reports missing): 37
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|
| 0-2 min | 13 | 83 / 276 | 78 / 181 | 28 / 70 |
| 2-5 min | 15 | 170 / 297 | 174 / 299 | 73 / 286 |
| 5-10 min | 22 | 415 / 607 | 417 / 606 | 10 / 371 |
| 10-20 min | 7 | 426 / 472 | 433 / 472 | 14 / 46 |

encounters: 29 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 12

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 15 | 9 | 8 | 366, 7 of 8 (88%) | 2, 0 of 8 (0%) | 8 of 8 (100%) | 5 of 8 (62%) |
| overtaking | 9 | 2 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| head_on | 2 | 1 | 1 | 244, 2 of 2 (100%) | - | - | - |
| unclear | 3 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |

### Cargo, tanker, passenger and high-speed craft only, CPA within 0.5 nm

```
reports: 68544, vessels: 132, assessed every 60 s; at most 60 vessels under way at once
risk: CPA within 0.50 nm within 20 min, range under 6 nm, both at 2 kn or more; cargo, tanker, passenger and high-speed craft only
pair-minutes at risk (Kalman predictor): 93; scored against the recorded future: 61; not scorable (reports missing): 32
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|
| 0-2 min | 9 | 40 / 402 | 43 / 413 | 7 / 159 |
| 2-5 min | 16 | 130 / 297 | 125 / 299 | 5 / 236 |
| 5-10 min | 25 | 259 / 492 | 271 / 513 | 10 / 31 |
| 10-20 min | 11 | 278 / 510 | 283 / 510 | 46 / 68 |

encounters: 11 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 10

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 7 | 7 | 5 | -20, 2 of 5 (40%) | -9, 0 of 5 (0%) | 3 of 5 (60%) | 4 of 5 (80%) |
| overtaking | 1 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| head_on | 3 | 3 | 3 | 113, 0 of 6 (0%) | - | - | - |
| unclear | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |

### Cargo, tanker, passenger and high-speed craft only, CPA within 0.25 nm

```
reports: 68544, vessels: 132, assessed every 60 s; at most 60 vessels under way at once
risk: CPA within 0.25 nm within 20 min, range under 6 nm, both at 2 kn or more; cargo, tanker, passenger and high-speed craft only
pair-minutes at risk (Kalman predictor): 52; scored against the recorded future: 28; not scorable (reports missing): 24
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|
| 0-2 min | 1 | 178 / 178 | 181 / 181 | 18 / 18 |
| 2-5 min | 8 | 248 / 297 | 248 / 299 | 21 / 286 |
| 5-10 min | 14 | 425 / 607 | 421 / 606 | 13 / 34 |
| 10-20 min | 5 | 454 / 472 | 457 / 472 | 19 / 46 |

encounters: 9 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 7

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 7 | 7 | 6 | 294, 5 of 6 (83%) | 2, 0 of 6 (0%) | 6 of 6 (100%) | 4 of 6 (67%) |
| overtaking | 2 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| head_on | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| unclear | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |

## skagen

### All vessels, CPA within 0.5 nm

```
reports: 181283, vessels: 224, assessed every 60 s; at most 149 vessels under way at once
risk: CPA within 0.50 nm within 20 min, range under 6 nm, both at 2 kn or more
pair-minutes at risk (Kalman predictor): 363; scored against the recorded future: 254; not scorable (reports missing): 109
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|
| 0-2 min | 35 | 8 / 101 | 11 / 100 | 4 / 100 |
| 2-5 min | 51 | 42 / 302 | 43 / 318 | 9 / 70 |
| 5-10 min | 59 | 97 / 384 | 92 / 395 | 18 / 137 |
| 10-20 min | 109 | 216 / 978 | 214 / 993 | 85 / 588 |

encounters: 81 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 51

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 43 | 34 | 20 | 13, 7 of 20 (35%) | 77, 9 of 20 (45%) | 8 of 20 (40%) | 10 of 20 (50%) |
| overtaking | 15 | 3 | 1 | 34, 0 of 1 (0%) | 197, 1 of 1 (100%) | 0 of 1 (0%) | - |
| head_on | 12 | 9 | 3 | -12, 1 of 6 (17%) | - | - | - |
| unclear | 11 | 5 | 1 | -182, 0 of 2 (0%) | - | - | - |

### All vessels, CPA within 0.25 nm

```
reports: 181283, vessels: 224, assessed every 60 s; at most 149 vessels under way at once
risk: CPA within 0.25 nm within 20 min, range under 6 nm, both at 2 kn or more
pair-minutes at risk (Kalman predictor): 153; scored against the recorded future: 116; not scorable (reports missing): 37
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|
| 0-2 min | 16 | 12 / 123 | 16 / 118 | 5 / 145 |
| 2-5 min | 20 | 104 / 463 | 105 / 474 | 15 / 254 |
| 5-10 min | 17 | 98 / 258 | 98 / 262 | 31 / 70 |
| 10-20 min | 63 | 241 / 704 | 239 / 705 | 108 / 420 |

encounters: 55 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 37

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 32 | 24 | 16 | 13, 4 of 16 (25%) | 77, 6 of 16 (38%) | 6 of 16 (38%) | 9 of 16 (56%) |
| overtaking | 5 | 3 | 1 | 34, 0 of 1 (0%) | 197, 1 of 1 (100%) | 0 of 1 (0%) | - |
| head_on | 11 | 8 | 4 | -20, 0 of 8 (0%) | - | - | - |
| unclear | 7 | 2 | 2 | -182, 0 of 4 (0%) | - | - | - |

### Cargo, tanker, passenger and high-speed craft only, CPA within 0.5 nm

```
reports: 181283, vessels: 224, assessed every 60 s; at most 149 vessels under way at once
risk: CPA within 0.50 nm within 20 min, range under 6 nm, both at 2 kn or more; cargo, tanker, passenger and high-speed craft only
pair-minutes at risk (Kalman predictor): 15; scored against the recorded future: 11; not scorable (reports missing): 4
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|
| 10-20 min | 11 | 4102 / 4730 | 4105 / 4714 | 438 / 544 |

encounters: 8 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 7

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 7 | 7 | 4 | 2150, 4 of 4 (100%) | -14, 0 of 4 (0%) | 4 of 4 (100%) | 0 of 4 (0%) |
| overtaking | 1 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| head_on | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| unclear | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |

### Cargo, tanker, passenger and high-speed craft only, CPA within 0.25 nm

```
reports: 181283, vessels: 224, assessed every 60 s; at most 149 vessels under way at once
risk: CPA within 0.25 nm within 20 min, range under 6 nm, both at 2 kn or more; cargo, tanker, passenger and high-speed craft only
pair-minutes at risk (Kalman predictor): 7; scored against the recorded future: 5; not scorable (reports missing): 2
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|
| 10-20 min | 5 | 4719 / 4730 | 4700 / 4714 | 472 / 531 |

encounters: 6 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 6

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 6 | 6 | 3 | 2739, 3 of 3 (100%) | 77, 0 of 3 (0%) | 3 of 3 (100%) | 0 of 3 (0%) |
| overtaking | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| head_on | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| unclear | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |

## bornholm

### All vessels, CPA within 0.5 nm

```
reports: 13982, vessels: 70, assessed every 60 s; at most 33 vessels under way at once
risk: CPA within 0.50 nm within 20 min, range under 6 nm, both at 2 kn or more
pair-minutes at risk (Kalman predictor): 0; scored against the recorded future: 0; not scorable (reports missing): 0
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|

encounters: 0 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 0

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | 0 of 0 (nan%) |
| overtaking | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| head_on | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| unclear | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |

### All vessels, CPA within 0.25 nm

```
reports: 13982, vessels: 70, assessed every 60 s; at most 33 vessels under way at once
risk: CPA within 0.25 nm within 20 min, range under 6 nm, both at 2 kn or more
pair-minutes at risk (Kalman predictor): 0; scored against the recorded future: 0; not scorable (reports missing): 0
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|

encounters: 0 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 0

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | 0 of 0 (nan%) |
| overtaking | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| head_on | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| unclear | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |

### Cargo, tanker, passenger and high-speed craft only, CPA within 0.5 nm

```
reports: 13982, vessels: 70, assessed every 60 s; at most 33 vessels under way at once
risk: CPA within 0.50 nm within 20 min, range under 6 nm, both at 2 kn or more; cargo, tanker, passenger and high-speed craft only
pair-minutes at risk (Kalman predictor): 0; scored against the recorded future: 0; not scorable (reports missing): 0
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|

encounters: 0 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 0

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | 0 of 0 (nan%) |
| overtaking | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| head_on | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| unclear | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |

### Cargo, tanker, passenger and high-speed craft only, CPA within 0.25 nm

```
reports: 13982, vessels: 70, assessed every 60 s; at most 33 vessels under way at once
risk: CPA within 0.25 nm within 20 min, range under 6 nm, both at 2 kn or more; cargo, tanker, passenger and high-speed craft only
pair-minutes at risk (Kalman predictor): 0; scored against the recorded future: 0; not scorable (reports missing): 0
```

| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|

encounters: 0 (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min before CPA): 0

| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossing | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | 0 of 0 (nan%) |
| overtaking | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| head_on | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |
| unclear | 0 | 0 | 0 | nan, 0 of 0 (nan%) | - | - | - |

Regression checks:

```
ok:   oresund: Kalman median CPA error at 0-2 min 41 m (<= 60)
ok:   oresund: Kalman median CPA error at 2-5 min 156 m (<= 200)
ok:   oresund: overtaking, give-way opened the distance more in 88% (>= 80%)
```
