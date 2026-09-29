# ADR 0005: Straight-line CPA and geometric COLREGs roles, checked against behaviour

Date: 2026-09-29
Status: Accepted; the pair search ("sweep over latitude" below) is superseded by [ADR 0006](0006-spatial-index.md)

## Context

M5 answers two questions for every pair of vessels under way: how close will
they pass, and when; and if that is too close, which of them has to keep out
of the way. A watchstander answers the first from radar or ECDIS (CPA and
TCPA) and the second from the steering rules of the COLREGs: Rule 13
(overtaking), Rule 14 (head-on) and Rule 15 (crossing).

There is no public ground truth for "risk of collision" or "who should have
given way". There is a recorded future: what the vessels actually did.

## Decision

- **CPA and TCPA from straight-line motion,** as on radar: each vessel keeps
  its current course and speed. Computed in a flat frame centred between the
  two vessels, so the answer does not depend on which vessel is first.
- **Motion from the tracker (M3)** when a track exists; dead reckoning from
  the last report is kept as a baseline in the evaluation.
- **Roles from geometry, following the rule text:**
  - overtaking (Rule 13): coming up from more than 22.5 degrees abaft the
    other vessel's beam; checked first, as Rule 13 applies
    "notwithstanding" Rules 14 and 15. The overtaking vessel gives way;
  - head-on (Rule 14): courses within 10 degrees of reciprocal and each
    vessel within 22.5 degrees of right ahead of the other. Both give way;
  - crossing (Rule 15): the vessel that has the other on her own starboard
    side gives way;
  - anything else (for example both vessels seeing each other to starboard)
    is reported as unclear, with both expected to act (Rules 7 and 8).
- **At risk** means both vessels at 2 kn or more, within 6 nm, and closing
  to within 0.5 nm within 20 minutes. These are common defaults, not rule
  text, and are parameters.
- **Pairs are found by a sweep over latitude,** examining only pairs within
  6 nm north-south of each other. A spatial index is M6's subject, with
  benchmarks.
- **Evaluate against behaviour:** if the roles are right, the give-way vessel
  should be the one whose own manoeuvring opens the passing distance
  ([collision-risk-evaluation.md](../collision-risk-evaluation.md)).

## Alternatives considered

- **Probabilistic CPA** from the tracks' covariance (probability that the
  pass is closer than a threshold). Better on noisy tracks, but on clean AIS
  the position uncertainty is metres against CPA errors of hundreds of
  metres caused by the vessels' own manoeuvres (see the evaluation). Worth it
  once predictions account for intent.
- **Ship-domain models** (an elliptical zone around each vessel, larger
  ahead). Closer to how mariners judge "too close", but they need
  per-vessel-size parameters that AIS alone gives unreliably.
- **Encoding Rules 9, 10 and 18** (narrow channels, traffic separation
  schemes, and the hierarchy that gives vessels fishing, sailing or
  restricted in their ability to manoeuvre right of way). Rule 18 needs
  navigational status, which AIS carries but is often left unset; Rules 9
  and 10 need charted channels and schemes. The evaluation shows where their
  absence matters.

## Consequences

- Course over ground stands in for heading when judging sectors; they can
  differ by a few degrees in wind or current.
- Roles are what the geometry says, not what the law says once Rules 9, 10
  and 18 apply. The evaluation reports results with and without fishing,
  pilot, tug and similar vessels, and names the waters where local practice
  dominates.
- A predicted CPA is the pass that would happen if nobody acted. In a real
  encounter somebody usually does, so the "error" at long horizons is partly
  the avoiding action itself.
