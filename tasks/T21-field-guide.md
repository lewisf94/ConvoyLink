# T21 — Field guide + five-unit tuning pass — closes v1.0

**Depends:** T20 · **Phase:** M6 · **Type:** documentation + parameter tuning

**Required reading:** everything in `docs/`; all field notes in
`tasks/STATUS.md` and task files.

## Goal

The document the convoy actually uses on trip day, plus a final pass over
tunables informed by real five-unit data.

## Deliverables

- `docs/08-field-guide.md`:
  - Per-car install: mounting, 12 V wiring with fuse, antenna placement
    (crib from docs/02 §Antenna, with photos/placeholders), speaker/mic
    positioning against road noise
  - Convoy setup table: who is U0–U4, initials, provisioning steps
  - Pre-drive check ritual (60 s): all units on `nt`, radio check round,
    GPS fix confirmation
  - On-the-road playbook: what STALE/GHOST dots mean and what to do,
    BUSY etiquette, regroup procedure when someone goes GHOST
    (last-known position + age is exactly what the radar preserves)
  - Troubleshooting quick table (symptom → docs/07 reference)
  - Legal note: 2.4 GHz ISM operation, region-dependent duty/power rules —
    owner verifies local regulations; no multi-km promises (docs/00 range
    reality restated honestly)
- Tuning pass (only with owner-supplied five-unit field data): revisit
  `convoy_cfg.h` values — beacon jitter, relay window, busy hangover,
  staleness thresholds, zoom hysteresis — change only what field notes
  justify, one commit per change with the observation cited
- Final `README.md` v1.0 update: status table complete, success-criteria
  scorecard from `docs/00` filled in with measured results

## Acceptance — CI

Green (doc task; any cfg change re-runs everything).

## Acceptance — hardware (owner)

- [ ] The five-car success criteria in `docs/00-brief.md` §Success
      criteria, executed as written, results recorded in the README
      scorecard

## Out of scope

New code beyond cfg constants. Stretch features remain stretch.
