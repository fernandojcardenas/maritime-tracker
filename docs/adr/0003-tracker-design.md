# ADR 0003: Per-vessel constant-velocity Kalman filter, gated by NIS

Date: 2026-09-29
Status: Accepted

## Context

AIS reports carry the vessel's identity, so unlike radar tracking there is
no association problem between reports and targets. The problems are
different: reports are sometimes wrong (GPS faults, spoofing, corrupted
messages), often incomplete (Class B units frequently omit speed and course),
and irregular in time (2 s to several minutes apart).

## Decision

- **One Kalman filter per MMSI**, constant-velocity model in a local
  east/north plane in metres, white-acceleration process noise.
- **Fuse what the report has:** position alone (2-D update) or position plus
  the velocity from speed and course (4-D update).
- **Gate on the normalised innovation squared** of the position against the
  chi-square 99.99% bound for 2 degrees of freedom (18.42). A report outside
  the gate is rejected and counted. Three rejections in a row mean the track
  is wrong, not the reports, so the track restarts.
- **Joseph-form covariance update** and explicit symmetrisation, so the
  covariance stays positive-definite over thousands of updates. If it ever
  fails, only that track restarts.
- **Re-anchor the local frame** after 20 km, keeping the flat-earth error
  negligible on long tracks.
- **Evaluate against baselines on held-out real data** (see
  [tracker-evaluation.md](../tracker-evaluation.md)), not on simulated tracks
  alone.

## Alternatives considered

- **Interacting multiple models (constant velocity plus coordinated turn).**
  Better on manoeuvres, but more parameters to tune and harder to explain.
  The evaluation shows turns cost the constant-velocity filter only a metre
  or two at short horizons; revisit if collision-risk work needs better turn
  prediction.
- **No filter, dead reckoning only.** As good on clean data (see the
  evaluation), but it has no uncertainty estimate, no defence against bad
  reports and nothing to offer when speed and course are missing.

## Consequences

- Each track has a covariance, which the collision-risk milestone needs to
  put uncertainty on closest-approach estimates.
- The gate's rejections are the first anomaly signal for milestone 4.
- Long silences are extrapolated at constant velocity, which the evaluation
  shows is wrong after about 10 minutes; milestone 4 treats them as dark
  periods instead.
