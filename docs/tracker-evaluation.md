# Tracker evaluation

How well does the tracker predict where each vessel will report next, and
does it survive bad input? This page answers that on real traffic. Every
number below was produced by `tools/evaluate_tracker.sh`, which CI also runs
(`.github/workflows/evaluate.yml`) and fails on regression.

## Data

- **Source:** Danish historical AIS, 2026-04-22, published free of charge by
  the Danish authorities at aisdata.ais.dk. The data is not redistributed
  here; `tools/fetch_dk_slice.sh` downloads the day and checks its SHA-256.
- **Slice:** Class A and B position reports in the Øresund strait
  (55.3–56.2 N, 12.3–13.2 E), 12:00–14:00 UTC: 144,414 reports from 411
  vessels. Many reports arrive more than once (several base stations hear
  the same transmission); those repeats are skipped by every method alike.
- **Split:** parameters are tuned on 12:00–13:00; every result below is from
  13:00–14:00, which tuning never saw.

## Method

For each report, three predictors say where the vessel is at that moment,
using only earlier reports:

| Method | Prediction |
|---|---|
| Hold | The vessel is where it last reported |
| Dead reckoning | The last report's position, moved along its speed and course |
| Kalman | The track's constant-velocity filter, fusing position with speed and course |

The error is the great-circle distance to the reported position, grouped by
how far ahead the prediction is. Reported positions carry some GPS noise of
their own, so no method can score zero.

**Robustness:** real input is not always this clean, so the same trajectories
are also run with seeded, labelled corruption: Gaussian position noise
(σ = 15 m), speed and course missing from 30% of reports (common on Class B
transponders), and 1% of reports thrown 500–2,000 m off. The corrupted
reports are fed to all three methods; every method is scored against the
clean position.

## Findings

Moving vessels (SOG ≥ 2 kn), held-out hour, median / 90th percentile error in metres:

| Input | Horizon | Reports | Hold | Dead reckoning | Kalman |
|---|---|---:|---:|---:|---:|
| Clean | 0.5–10 s | 6,453 | 22 / 53 | **1 / 8** | 3 / 10 |
| Clean | 10–30 s | 4,094 | 53 / 83 | **2 / 12** | 3 / 12 |
| Clean | 30–60 s | 805 | 91 / 130 | **9 / 48** | 11 / 58 |
| Noisy, 30% without speed/course | 0.5–10 s | 6,453 | 31 / 63 | 21 / 45 | **7 / 19** |
| Noisy, 30% without speed/course | 10–30 s | 4,094 | 54 / 93 | 24 / 67 | **6 / 22** |
| Noisy, 30% without speed/course | 30–60 s | 805 | 92 / 141 | 31 / 117 | **22 / 81** |

1. **On clean data, dead reckoning is as good as the filter, slightly
   better.** AIS speed and course come straight from the ship's GPS and are
   accurate, and these positions are clean, so projecting the last report
   forward is already a strong predictor. The filter blends in older
   reports, which costs a metre or two when a vessel changes course.
2. **With noisy or incomplete input, the filter wins clearly:** 3–4 times
   lower median error at horizons up to 30 s, and a much smaller tail. This is
   the case the filter is for: it averages noise over several reports and
   keeps a velocity estimate when a report has no speed or course.
3. **The gate rejects about 89% of wild reports.** With 1% of reports thrown
   500–2,000 m off, it rejected 358 of 400 in the held-out hour. The price is
   genuine reports rejected by mistake: 1.64% when the filter is told
   positions are good to 5 m, 0.42% when told 25 m. A tight noise setting
   makes the gate suspicious of real manoeuvres. That trade-off is the
   starting point for anomaly detection (milestone 4).
4. **Long silences are not handled well yet.** In the 600–1,800 s bucket
   there are only two moving-vessel cases, and in both the filter was about
   1.3 km off. One vessel turned around during the gap and the other got
   underway after drifting. A constant-velocity model shouldn't extrapolate
   that far. Treating long silences as "dark" periods is part of milestone 4.

Chosen parameters: process noise σₐ = 0.05 m/s², velocity noise 0.1 m/s,
position noise 5 m for clean GPS and 25 m for degraded input. The position
noise describes the sensor, so in live use it comes from each report's
position-accuracy flag (10 m or 25 m).

## Full output

## Tuning (first hour)

### Clean input

| sigma_a (m/s^2) | position sigma (m) | velocity sigma (m/s) | Kalman score (m) | dead reckoning score (m) |
|---:|---:|---:|---:|---:|
| 0.020 | 3.0 | 0.10 | 13.61 | 11.12 |
| 0.020 | 3.0 | 0.30 | 18.67 | 11.12 |
| 0.020 | 5.0 | 0.10 | 12.11 | 11.12 |
| 0.020 | 5.0 | 0.30 | 14.48 | 11.12 |
| 0.020 | 10.0 | 0.10 | 13.15 | 11.12 |
| 0.020 | 10.0 | 0.30 | 12.83 | 11.12 |
| 0.050 | 3.0 | 0.10 | 12.96 | 11.12 |
| 0.050 | 3.0 | 0.30 | 16.36 | 11.12 |
| 0.050 | 5.0 | 0.10 | 11.96 | 11.12 |
| 0.050 | 5.0 | 0.30 | 13.44 | 11.12 |
| 0.050 | 10.0 | 0.10 | 13.20 | 11.12 |
| 0.050 | 10.0 | 0.30 | 12.94 | 11.12 |
| 0.100 | 3.0 | 0.10 | 12.60 | 11.12 |
| 0.100 | 3.0 | 0.30 | 16.34 | 11.12 |
| 0.100 | 5.0 | 0.10 | 11.99 | 11.12 |
| 0.100 | 5.0 | 0.30 | 13.45 | 11.12 |
| 0.100 | 10.0 | 0.10 | 13.26 | 11.12 |
| 0.100 | 10.0 | 0.30 | 12.56 | 11.12 |

score: mean over prediction horizons of the median error on moving vessels

### Noisy input (--noise 15 --drop-velocity 0.3 --outliers 0.01)

| sigma_a (m/s^2) | position sigma (m) | velocity sigma (m/s) | Kalman score (m) | dead reckoning score (m) |
|---:|---:|---:|---:|---:|
| 0.020 | 15.0 | 0.10 | 19.64 | 33.80 |
| 0.020 | 15.0 | 0.30 | 20.50 | 33.80 |
| 0.020 | 25.0 | 0.10 | 18.94 | 33.80 |
| 0.020 | 25.0 | 0.30 | 19.47 | 33.80 |
| 0.050 | 15.0 | 0.10 | 22.09 | 33.80 |
| 0.050 | 15.0 | 0.30 | 21.06 | 33.80 |
| 0.050 | 25.0 | 0.10 | 20.06 | 33.80 |
| 0.050 | 25.0 | 0.30 | 20.02 | 33.80 |
| 0.100 | 15.0 | 0.10 | 23.60 | 33.80 |
| 0.100 | 15.0 | 0.30 | 22.42 | 33.80 |
| 0.100 | 25.0 | 0.10 | 22.44 | 33.80 |
| 0.100 | 25.0 | 0.30 | 21.09 | 33.80 |

score: mean over prediction horizons of the median error on moving vessels

## Held-out hour: clean

```
fixes loaded: 144414
perturbation: noise 0.0 m, velocity dropped 0%, outliers 0.0% (seed 42)
sigma_a 0.050 m/s^2, position sigma 5.0 m, velocity sigma 0.10 m/s
tracker: 144414 fixes, 422 tracks started, 79251 updates, 63795 duplicates, 0 out of order, 946 rejected by the gate, 139 restarts, 48 expired, 5 re-anchored, 0 numeric resets
evaluated reports: 38234, inside the gate: 99.16%
```
gate: rejected 0 of 0 injected outliers (0.0%) and 493 of 38416 genuine reports (1.28%)

All vessels

| Horizon | Reports | Hold median / p90 (m) | Dead reckoning median / p90 (m) | Kalman median / p90 (m) |
|---|---:|---:|---:|---:|
| 0.5-10 s | 13985 | 2 / 44 | 0 / 6 | 1 / 7 |
| 10-30 s | 16396 | 1 / 55 | 0 / 5 | 1 / 5 |
| 30-60 s | 2328 | 2 / 111 | 2 / 27 | 4 / 33 |
| 60-180 s | 3469 | 0 / 5 | 1 / 8 | 3 / 18 |
| 180-600 s | 2019 | 1 / 4 | 1 / 9 | 4 / 25 |
| 600-1800 s | 37 | 0 / 15 | 0 / 36 | 14 / 188 |

Moving vessels (SOG >= 2 kn at the report)

| Horizon | Reports | Hold median / p90 (m) | Dead reckoning median / p90 (m) | Kalman median / p90 (m) |
|---|---:|---:|---:|---:|
| 0.5-10 s | 6453 | 22 / 53 | 1 / 8 | 3 / 10 |
| 10-30 s | 4094 | 53 / 83 | 2 / 12 | 3 / 12 |
| 30-60 s | 805 | 91 / 130 | 9 / 48 | 11 / 58 |
| 60-180 s | 194 | 184 / 255 | 21 / 141 | 22 / 154 |
| 180-600 s | 10 | 315 / 580 | 39 / 538 | 40 / 566 |
| 600-1800 s | 2 | 100 / 100 | 100 / 100 | 1279 / 1279 |

## Held-out hour: noisy

```
fixes loaded: 144414
perturbation: noise 15.0 m, velocity dropped 30%, outliers 0.0% (seed 42)
sigma_a 0.050 m/s^2, position sigma 25.0 m, velocity sigma 0.10 m/s
tracker: 144414 fixes, 422 tracks started, 79804 updates, 64107 duplicates, 0 out of order, 81 rejected by the gate, 10 restarts, 48 expired, 15 re-anchored, 0 numeric resets
evaluated reports: 38234, inside the gate: 99.92%
```
gate: rejected 0 of 0 injected outliers (0.0%) and 41 of 38247 genuine reports (0.11%)

All vessels

| Horizon | Reports | Hold median / p90 (m) | Dead reckoning median / p90 (m) | Kalman median / p90 (m) |
|---|---:|---:|---:|---:|
| 0.5-10 s | 13985 | 22 / 51 | 19 / 38 | 6 / 17 |
| 10-30 s | 16396 | 21 / 60 | 19 / 39 | 6 / 19 |
| 30-60 s | 2328 | 25 / 113 | 21 / 60 | 24 / 73 |
| 60-180 s | 3469 | 19 / 38 | 19 / 36 | 28 / 139 |
| 180-600 s | 2019 | 18 / 33 | 18 / 34 | 54 / 244 |
| 600-1800 s | 37 | 15 / 36 | 15 / 44 | 368 / 774 |

Moving vessels (SOG >= 2 kn at the report)

| Horizon | Reports | Hold median / p90 (m) | Dead reckoning median / p90 (m) | Kalman median / p90 (m) |
|---|---:|---:|---:|---:|
| 0.5-10 s | 6453 | 31 / 63 | 21 / 45 | 7 / 19 |
| 10-30 s | 4094 | 54 / 93 | 24 / 67 | 6 / 22 |
| 30-60 s | 805 | 92 / 141 | 31 / 117 | 22 / 81 |
| 60-180 s | 194 | 183 / 258 | 52 / 226 | 46 / 169 |
| 180-600 s | 10 | 318 / 594 | 85 / 594 | 96 / 694 |
| 600-1800 s | 2 | 88 / 88 | 88 / 88 | 1293 / 1293 |

## Held-out hour: outliers

```
fixes loaded: 144414
perturbation: noise 0.0 m, velocity dropped 0%, outliers 1.0% (seed 42)
sigma_a 0.050 m/s^2, position sigma 5.0 m, velocity sigma 0.10 m/s
tracker: 144414 fixes, 422 tracks started, 78673 updates, 63399 duplicates, 0 out of order, 1920 rejected by the gate, 178 restarts, 48 expired, 5 re-anchored, 0 numeric resets
evaluated reports: 38234, inside the gate: 97.93%
```
gate: rejected 358 of 400 injected outliers (89.5%) and 626 of 38205 genuine reports (1.64%)

All vessels

| Horizon | Reports | Hold median / p90 (m) | Dead reckoning median / p90 (m) | Kalman median / p90 (m) |
|---|---:|---:|---:|---:|
| 0.5-10 s | 13985 | 2 / 45 | 0 / 6 | 1 / 7 |
| 10-30 s | 16396 | 1 / 57 | 1 / 5 | 1 / 5 |
| 30-60 s | 2328 | 2 / 113 | 2 / 30 | 4 / 37 |
| 60-180 s | 3469 | 1 / 5 | 1 / 9 | 3 / 25 |
| 180-600 s | 2019 | 1 / 4 | 1 / 9 | 4 / 36 |
| 600-1800 s | 37 | 0 / 15 | 0 / 36 | 18 / 264 |

Moving vessels (SOG >= 2 kn at the report)

| Horizon | Reports | Hold median / p90 (m) | Dead reckoning median / p90 (m) | Kalman median / p90 (m) |
|---|---:|---:|---:|---:|
| 0.5-10 s | 6453 | 22 / 54 | 1 / 9 | 3 / 10 |
| 10-30 s | 4094 | 53 / 86 | 2 / 13 | 3 / 12 |
| 30-60 s | 805 | 92 / 133 | 9 / 61 | 11 / 63 |
| 60-180 s | 194 | 185 / 257 | 22 / 179 | 23 / 192 |
| 180-600 s | 10 | 315 / 580 | 39 / 538 | 40 / 587 |
| 600-1800 s | 2 | 100 / 100 | 100 / 100 | 1279 / 1279 |

## Held-out hour: combined

```
fixes loaded: 144414
perturbation: noise 15.0 m, velocity dropped 30%, outliers 1.0% (seed 42)
sigma_a 0.050 m/s^2, position sigma 25.0 m, velocity sigma 0.10 m/s
tracker: 144414 fixes, 422 tracks started, 79265 updates, 63706 duplicates, 0 out of order, 1021 rejected by the gate, 40 restarts, 48 expired, 15 re-anchored, 0 numeric resets
evaluated reports: 38234, inside the gate: 98.73%
```
gate: rejected 354 of 397 injected outliers (89.2%) and 161 of 38040 genuine reports (0.42%)

All vessels

| Horizon | Reports | Hold median / p90 (m) | Dead reckoning median / p90 (m) | Kalman median / p90 (m) |
|---|---:|---:|---:|---:|
| 0.5-10 s | 13985 | 22 / 53 | 19 / 38 | 6 / 16 |
| 10-30 s | 16396 | 21 / 63 | 19 / 40 | 6 / 19 |
| 30-60 s | 2328 | 26 / 114 | 21 / 70 | 25 / 74 |
| 60-180 s | 3469 | 19 / 39 | 19 / 36 | 27 / 148 |
| 180-600 s | 2019 | 18 / 35 | 19 / 36 | 46 / 281 |
| 600-1800 s | 37 | 23 / 40 | 23 / 44 | 309 / 1070 |

Moving vessels (SOG >= 2 kn at the report)

| Horizon | Reports | Hold median / p90 (m) | Dead reckoning median / p90 (m) | Kalman median / p90 (m) |
|---|---:|---:|---:|---:|
| 0.5-10 s | 6453 | 32 / 64 | 21 / 47 | 6 / 17 |
| 10-30 s | 4094 | 55 / 93 | 25 / 68 | 6 / 22 |
| 30-60 s | 805 | 91 / 141 | 30 / 121 | 22 / 85 |
| 60-180 s | 194 | 181 / 267 | 47 / 236 | 46 / 227 |
| 180-600 s | 10 | 322 / 577 | 69 / 550 | 81 / 932 |
| 600-1800 s | 2 | 95 / 95 | 95 / 95 | 1269 / 1269 |

