# Collision-risk evaluation

Two questions, answered on real traffic: how accurate is the predicted
closest point of approach (CPA), and do the roles the COLREGs geometry
assigns (who gives way, who stands on) match what vessels actually do?
Every Danish number below was produced by `tools/evaluate_collision_risk.sh`,
which CI also runs (`.github/workflows/evaluate.yml`) and fails on
regression. The design is in [ADR 0005](adr/0005-collision-risk.md).

## What is computed

Every minute, for every pair of vessels under way (2 kn or more) within 6 nm:
CPA and time to CPA (TCPA) from straight-line motion, and if they will pass
within 0.5 nm within 20 minutes, the encounter type and roles:

| Encounter | Geometry | Who keeps out of the way |
|---|---|---|
| Overtaking (Rule 13) | Coming up from more than 22.5° abaft the other's beam | The overtaking vessel |
| Head-on (Rule 14) | Courses within 10° of reciprocal, each within 22.5° of the other's bow | Both, to starboard |
| Crossing (Rule 15) | Neither of the above | The vessel that has the other on her starboard side |
| Unclear | Each sees the other on the same side (e.g. passing starboard to starboard) | Both, in doubt |

## Data

The Danish slices from the [anomaly evaluation](anomaly-evaluation.md)
(Danish historical AIS, 2026-04-22): Øresund 12:00–14:00, Great Belt
16:00–18:00, Skagen 06:00–08:00 and Bornholm 20:00–22:00 (no vessel pair came
within 0.5 nm there). Ship types come from the same files.

## 1. How accurate is the predicted CPA?

At every minute a pair is at risk, the predicted CPA is compared with how
close the two vessels really came, from their recorded positions over the
following minutes. Øresund, all vessels (1,262 scored pair-minutes; 894
could not be scored because one vessel's reports had a gap of more than
2 minutes, mostly Class B units):

| Time to CPA | Pair-minutes | Dead reckoning: error median / p90 (m) | Kalman track: error median / p90 (m) | TCPA error, Kalman median / p90 (s) |
|---|---:|---:|---:|---:|
| 0–2 min | 478 | 41 / 217 | 41 / 213 | 26 / 147 |
| 2–5 min | 400 | 155 / 438 | 156 / 435 | 67 / 268 |
| 5–10 min | 289 | 233 / 819 | 242 / 818 | 158 / 526 |
| 10–20 min | 95 | 405 / 2,669 | 409 / 2,648 | 231 / 633 |

1. **The error grows with the horizon because vessels manoeuvre,** often in
   response to exactly this situation. A CPA is the pass that would happen
   if nobody acted. At Skagen, for 11 pair-minutes between commercial
   vessels predicting a pass within 0.5 nm 10–20 minutes ahead, the
   prediction was off by a median 4.1 km: the vessels passed far wider,
   because the give-way vessels altered (their median contribution, below,
   was 2.15 km).
2. **The Kalman track and dead reckoning are equal** on this clean data, as
   in the [tracker evaluation](tracker-evaluation.md): AIS speed and course
   are already accurate. The track matters when reports are noisy or lack
   speed and course.

## 2. Do the roles match what vessels do?

**Method.** For each encounter (the first minute a pair is at risk) that is
*developing*, first at risk at least 1 nm apart and at least 4 minutes before
CPA, so there was room and time to act (Rule 16), each vessel's
**contribution** is measured: the real passing distance minus the passing
distance had that vessel held its course and speed while the other did what
it really did. Positive means its own manoeuvring opened the distance. If the
roles are right, the give-way vessel should contribute more than the
stand-on vessel, which Rule 17 tells to keep its course and speed.

**Results**, "give-way opened the distance more than stand-on":

| Encounters (developing, scored) | CPA within 0.5 nm | CPA within 0.25 nm |
|---|---:|---:|
| Overtaking, Øresund, all vessels | 15 of 17 | 12 of 12 |
| Overtaking, Øresund, commercial only | 7 of 7 | 8 of 8 |
| Crossing, Great Belt and Skagen, commercial only | 7 of 9 | 9 of 9 |
| Crossing, Great Belt and Skagen, all vessels | 12 of 27 | 14 of 24 |
| Crossing, Øresund, commercial only | 27 of 52 | 19 of 34 |
| Crossing, Øresund, all vessels | 42 of 77 | 29 of 55 |

"Commercial" means both vessels are cargo ships, tankers, passenger ships or
high-speed craft.

1. **Overtaking matches the rules almost perfectly:** the overtaking vessel
   opens the distance, the vessel being overtaken holds on.
2. **Crossings between commercial vessels in open water match too** (Great
   Belt and Skagen), but the sample is small: 9 encounters.
3. **With all vessel types, crossings are close to a coin toss.** The
   commercial-only split was added *after* seeing this, on the hypothesis
   that Rule 18 was the cause: vessels fishing, and pilot boats, tugs and
   dredgers at work, have right of way whatever the crossing geometry says.
   Fishing is the most common ship type in these slices (107,467 of 408,223
   reports). The split supports the hypothesis outside Øresund; it is a
   post-hoc finding and is labelled as one.
4. **Øresund crossings are about half and half even between commercial
   vessels.** 129 of the 132 developing commercial crossings there involve a
   passenger ship, 91 involve two, and the vessels in them most often are
   harbour boats inside Copenhagen harbour and a ferry on the
   Helsingør–Helsingborg route. Their paths are fixed and local practice,
   not Rule 15, decides who waits.
5. **Rule 15 also says the give-way vessel should avoid crossing ahead.** At
   the real closest approach the give-way vessel was abaft the stand-on
   vessel's beam in 5 of 7 Great Belt crossings, 10 of 20 at Skagen and 25
   of 74 in Øresund.
6. **Head-on encounters rarely show action** (3 of 24 vessels in Øresund
   opened the distance by 0.1 nm or more). Most are vessels in two-way
   routes that are already offset and pass port to port as planned.

## Live Norwegian hour

`mt-ingest` assesses risk every minute of the live stream. On the one-hour
recording from the M2 live run (132,270 records, 3,752 vessels) it found
2,020 encounters: 938 crossing, 450 overtaking, 337 head-on and 295 unclear.
The whole hour, with tracking, anomaly detection and risk assessment,
replays in 1.4 seconds.

## Limitations

- Roles are geometric: Rules 9 (narrow channels), 10 (traffic separation
  schemes) and 18 (the hierarchy of vessels) are not modelled.
- Course over ground stands in for heading.
- The contribution measure counts any departure from a straight course,
  including a routine turn along a route (rounding the Skaw at Skagen, for
  example), as action.
- Samples outside Øresund are small. The thresholds (0.5 nm, 20 minutes,
  1 nm and 4 minutes for "developing", 0.1 nm for "opened") are common
  defaults, not rule text.
- No VHF or VTS data: vessels that agreed a passing by radio look the same
  as vessels that ignored the rules.

## Reproduce

```sh
tools/fetch_dk_slice.sh data/oresund-2026-04-22.csv data/   # 830 MB download, SHA-256 checked
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target mt-risk
tools/evaluate_collision_risk.sh build/mt-risk data/
```

Unedited output: [evidence/m5-collision-risk-evaluation-2026-09-29.md](evidence/m5-collision-risk-evaluation-2026-09-29.md).
