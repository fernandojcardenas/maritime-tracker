# Anomaly evaluation

Does the detector find anomalies that are there, and stay quiet when nothing
is wrong? This page answers that on real traffic. Every Danish number below
was produced by `tools/evaluate_anomalies.sh`, which CI also runs
(`.github/workflows/evaluate.yml`) and fails on regression. The design is in
[ADR 0004](adr/0004-anomaly-rules.md).

## What is flagged

| Flag | Rule |
|---|---|
| Impossible speed | Reported speed over ground above 50 kn (search-and-rescue aircraft, MMSI 111MIDxxx, are not checked) |
| Position jump | Farther from the vessel's last position than it could have travelled: at 50 kn, or at 15 kn above the higher of the two reported speeds when both reports carry one, plus 500 m for GPS error. A single report out of line with the reports on both sides of it is the one flagged |
| Gap | Silent for 10 minutes or more after being heard at least 3 times in the previous 10 minutes, while moving (≥ 2 kn). Minutes in which the whole network went quiet do not count; a silence that ends within 2 minutes of the silences of 3 or more other vessels is coverage coming back, not a vessel |
| Identity conflict | One MMSI reporting from two places that cannot both be right, with 3 or more reports from the second place while the first keeps reporting |

## Data

All Danish slices are Class A and B position reports from the Danish
historical AIS data for 2026-04-22 (aisdata.ais.dk, published free of charge
by the Danish authorities; downloaded and checked by SHA-256, not
redistributed). `tools/fetch_dk_slice.sh` extracts them.

| Slice | Area | Time (UTC) | Reports | Vessels | Role |
|---|---|---|---:|---:|---|
| Øresund | 55.3–56.2 N, 12.3–13.2 E | 12:00–14:00 | 144,414 | 411 | Rules developed here |
| Great Belt | 55.0–55.8 N, 10.6–11.4 E | 16:00–18:00 | 68,544 | 133 | Evaluation |
| Skagen | 57.3–58.0 N, 10.0–11.2 E | 06:00–08:00 | 181,283 | 224 | Evaluation |
| Bornholm | 54.9–55.4 N, 14.5–15.3 E | 20:00–22:00 | 13,982 | 70 | Final hold-out, run once |
| Norway, live | Norwegian coast to 82 N | 13:39–14:39, 2026-09-29 | 132,270 | 3,752 | Live run from M2 (BarentsWatch, NLOD) |

## Method

**Flags on the data as recorded.** Public data has no labels, so every flag
raised on unmodified data is listed and explained below.

**Planted anomalies.** Seeded, labelled cases are planted into a copy of
each slice, 5 of each kind per seed, seeds 1–20, each on a different vessel
that reports at least 60 times over at least 40 minutes:

| Planted case | How |
|---|---|
| Position jump | One transmission (every copy of it) moved 3–30 km in a random direction |
| Impossible speed | One transmission's speed set to 55–100 kn |
| Gap | A vessel's reports removed for 15–40 minutes, starting from a report where it was moving |
| Identity conflict | For 10 minutes, another vessel's reports (at least 10 in that time, at least 5 km away) copied under this vessel's MMSI |

A planted case counts as found if a flag of the same kind is raised for that
vessel in its window. **Extra flags** are flags that appear only in the
planted copy and match no planted case: false alarms caused by planting.

## How the rules were developed, and what that means for the numbers

The rules changed three times after looking at data, so only the last slice
is a clean hold-out:

| Step | Data | What it showed | Change |
|---|---|---|---|
| 1 | Øresund | Moored and Class B vessels reporting every 3–6 minutes let 3–5 km jumps through at 50 kn, and flags landed on the report *after* a bad one. A planted conflict was missed because the real vessel was briefly quiet and the second transmitter took over | Speed-aware limit; "out and back" blames the odd report; the second position takes over only after the first is silent for 10 minutes |
| 2 | Great Belt | No reports at all from the whole slice from 17:00 to 17:09 (a receiver outage): 10 vessels flagged as gaps at once | Outage minutes do not count |
| 3 | Skagen | Passed with no change | — |
| 4 | Norway, live | Three search-and-rescue helicopters flagged 67 times; 130 gap flags from satellite passes and from positions older than the stream | SAR aircraft not checked; the regular-reporting and coincident-return conditions; silences that began before listening are not judged |
| 5 | Bornholm | Run once, after the rules were frozen | — |

All results below use the final rules. The Øresund, Great Belt and live
numbers are therefore optimistic in the usual way; Skagen passed before the
last change and was re-run after it; Bornholm is the honest check.

## Results

Clear-cut planted cases (sizes as in the table above):

| Slice | Flags as recorded | Speed | Jump | Gap | Conflict | Extra flags |
|---|---:|---:|---:|---:|---:|---:|
| Øresund | 5 | 100/100 | 100/100 | 97/100 | 98/100 | 3 |
| Great Belt | 1 | 100/100 | 100/100 | 82/100 | 99/100 | 1 |
| Skagen | 4 | 100/100 | 100/100 | 100/100 | 99/100 | 1 |
| Bornholm (hold-out) | 0 | 73/73 | 78/78 | 62/62 | 59/59 | 0 |

(Bornholm is small: fewer vessels qualify for planting.)

All 18 gaps missed on the Great Belt overlap the 10-minute receiver outage,
by design: silence while the receivers are down is not the vessel's.

**Where detection stops** (all four slices pooled, same method, smaller
cases):

| Planted case | Planted | Found | Recall |
|---|---:|---:|---:|
| Jump 0.5–1 km | 378 | 197 | 52.1% |
| Jump 1–2 km | 378 | 317 | 83.9% |
| Jump 2–3 km | 378 | 354 | 93.7% |
| Jump 3–30 km | 378 | 378 | 100.0% |
| Gap 5–10 min (shorter than the rule) | 362 | 14 | 3.9% |
| Gap 10–15 min | 362 | 347 | 95.9% |
| Gap 15–40 min | 362 | 341 | 94.2% |
| Second transmitter 1–2 km away | 148 | 140 | 94.6% |
| Second transmitter 2–5 km away | 215 | 214 | 99.5% |
| Second transmitter 5+ km away | 359 | 355 | 98.9% |
| Speed 50–55 kn | 373 | 373 | 100.0% |

1. **Jumps under about 2 km are often physically possible.** Between two
   reports 3 minutes apart without a speed, a vessel could have moved 4.6 km
   at 50 kn, so a smaller jump cannot be called impossible. For a vessel
   reporting every few seconds with a speed, the limit is a few hundred
   metres above the 500 m slack.
2. **Gaps shorter than 10 minutes are not flagged,** on purpose; the few
   found (3.9%) are where the vessel's normal reporting interval stretched
   the silence past 10 minutes.
3. **Identity conflicts are found even 1–2 km apart,** because both
   transmitters keep reporting (the planted one at least once a minute) and
   their positions cannot both be right.

## Flags on the data as recorded

| Slice | Flag | What it is |
|---|---|---|
| Øresund | 4 gaps | Three Class B passenger boats in Copenhagen harbour, silent 11–58 minutes and back within 1.1 km of where they went quiet: consistent with switching off at the quay (the last report still showed ≥ 2 kn) |
| Øresund | 1 gap | A cargo ship at the slice's western edge (12.30 E), back after 96 minutes 1.5 km away: it left the slice's box, not the air. An artefact of cutting a rectangle out of the data |
| Great Belt | 1 jump | A Class B unit 530 m off within two seconds: a GPS glitch |
| Skagen | 1 speed | A fishing vessel reporting 65.7 kn: a faulty speed |
| Skagen | 1 jump | MMSI 970xxxxxx, an AIS-SART (search-and-rescue transmitter), 666 m off within a second: a GPS glitch |
| Skagen | 2 gaps | A fishing vessel (15 minutes) and a passenger vessel back 5 m from where it went quiet after 27 minutes |
| Bornholm | none | |

None of these is a planted case, and none looks like wrongdoing: they are
the faults and habits that real AIS contains. They are also few: 10 flags in
eight hours of real traffic.

## Live Norwegian hour

The detector runs inside `mt-ingest` as reports arrive, in arrival order,
not sorted; on the recording from the M2 live run it gives the same result
as the offline tool on sorted data:

```
mt-ingest --replay live.jsonl --format barentswatch --speed 0 --listening-since 1790689162 --anomalies anomalies.jsonl
anomalies        2 impossible speed, 0 position jumps, 13 gaps, 0 identity conflicts
not flagged      290 silences begun before listening, 19 coverage returned, 0 network outage, 36 not heard regularly; 141 SAR aircraft reports
```

- **Before step 4 above,** the same hour raised 18 speed, 51 jump and 130
  gap flags. Three search-and-rescue helicopters, over the North Sea and
  the coast, accounted for 16 of the speed flags and all 51 jumps.
- **Speed:** one vessel in the Barents Sea reporting 102.2 kn (the AIS code
  for "102.2 kn or more", an equipment fault), and one high-speed craft at
  51.3 kn, which is probably real: 50 kn is at the edge for the fastest
  craft, and the detector does not use ship type.
- **13 gaps,** 10–25 minutes each: 6 passenger vessels (mostly fjord
  ferries), 2 fishing and 5 cargo vessels; 8 of the 13 were back within
  1.1 km of where they went quiet. Radio shadows in fjords and switching off
  at a quay both fit, and AIS alone cannot tell them apart from a
  transponder switched off at sea. That needs a coverage map (ADR 0004).

## Limitations

- Sparse reporters without a speed hide jumps smaller than 50 kn times the
  reporting interval.
- Four or more vessels switching off and on together look like coverage.
- The 50 kn limit is close to the top speed of the fastest craft.
- Reachability is a straight line: a track across land is not checked.
- Thresholds are fixed by physics and AIS reporting rules, not learned from
  data, so they do not adapt to a region's traffic.

## Reproduce

```sh
tools/fetch_dk_slice.sh data/oresund-2026-04-22.csv data/   # 830 MB download, SHA-256 checked
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target mt-anomaly
tools/evaluate_anomalies.sh build/mt-anomaly data/
```

The live hour is the recording in the artifact of Live feed run #3; replay it
with the `mt-ingest` command above after `cat recordings/*.jsonl > live.jsonl`.

Unedited outputs behind this page: [Danish slices](evidence/m4-anomaly-evaluation-2026-09-29.md),
[live hour](evidence/m4-live-anomalies-2026-09-29.txt).
