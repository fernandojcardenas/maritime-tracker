# ADR 0004: Anomalies as physical rules, with explicit coverage checks

Date: 2026-09-29
Status: Accepted

## Context

M4 flags behaviour worth a person's attention: a vessel reporting a speed or
a position it cannot have, a vessel that goes quiet while under way, and one
identity used by two transmitters at once. Whoever reviews a flag needs to
know why it was raised and must be able to check it by hand. A score from a
model that nobody can explain gets ignored.

The biggest source of false alarms is not the vessels but the receivers.
Coastal receiver networks have outages and blind spots, satellite AIS hears a
vessel only on each pass, and a live stream opens with positions that may be
hours old. None of these is the vessel's doing.

## Decision

- **Every rule is a physical limit with a number an operator can check:**
  no vessel faster than 50 kn; no movement farther than the vessel could have
  travelled (at 50 kn, or at 15 kn above the higher of the two reported
  speeds when both reports carry one) plus 500 m for GPS error; no silence of
  10 minutes or more while moving.
- **Blame the right report.** A report that disagrees with the reports on
  both sides of it is flagged itself ("out and back"), not the report that
  returns to the true track. Repeats of one transmission, heard by several
  base stations, count as one report.
- **Identity conflict needs persistence.** A position the vessel cannot
  reach opens a second position; three reports from it while the first keeps
  reporting raise one conflict. If the first position instead goes quiet for
  10 minutes, the second takes over (the first was wrong, or the vessel
  really moved).
- **A silence counts only if the vessel's silence is informative.** It must
  have been moving and heard at least 3 times in the 10 minutes before;
  minutes in which the whole network delivered under a quarter of its usual
  reports do not count towards the 10 minutes; a silence that ends together
  with those of 3 or more other vessels (within 2 minutes) is coverage coming
  back, not a vessel; a silence that began before the detector started
  listening is not judged. Gap flags are held for 2 minutes so the
  other vessels can arrive.
- **Search-and-rescue aircraft (MMSI 111MIDxxx) are not checked.** They fly
  at 100+ kn by design.
- **Rules are evaluated on real traffic with planted, labelled anomalies,**
  on slices the rules were not developed on, and on a final slice that was
  run once after the rules were frozen
  ([anomaly-evaluation.md](../anomaly-evaluation.md)).

## Alternatives considered

- **Use the tracker's gate (ADR 0003) as the jump detector.** The gate is
  tuned to keep bad reports out of the track, and on clean real data it
  rejects 1.3% of genuine reports (tracker evaluation, held-out hour). On the
  unmodified two-hour Øresund slice it rejected 946 reports; the jump rule
  flags none there. As alerts that is far too many, and "the innovation was 4.3 sigma"
  is not something an operator can check.
- **A learned model (isolation forest, autoencoder on tracks).** Could find
  patterns the rules miss, but needs labelled real incidents to evaluate,
  which public data does not have, and cannot explain its flags. The rules
  are also the features such a model would start from; revisit once the
  rules' false alarms are understood.
- **A receiver coverage map** (typical reception per grid cell, built from
  history) would explain silences better than the coincidence rule. It needs
  days of data per region; the coincidence and outage rules work from the
  first hour and without knowing where the receivers are.

## Consequences

- Every flag carries the number that broke the limit (speed, distance,
  silence length, distance between the two transmitters).
- Small jumps between sparse reports are physically possible and are not
  flagged: a Class B unit reporting every 6 minutes without a speed could
  have moved 9 km at 50 kn.
- If four or more vessels switch their transponders off and on together,
  the coincidence rule will treat it as coverage. That is the price of not
  alerting on every satellite pass.
- Silences in radio shadows (fjords, behind islands) still produce flags;
  telling those apart needs the coverage map above.
