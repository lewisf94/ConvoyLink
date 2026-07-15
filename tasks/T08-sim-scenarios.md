# T08 — Simulator scenarios + behaviour checks

**Depends:** T07 · **Phase:** M2
**Required reading:** `docs/05-gps-geo.md` §Staleness; `docs/03` §Beacon relay

## Goal

A scenario library that exercises every radar behaviour tier, plus scripted
assertions so CI catches regressions in the full pipeline (not just unit
tests).

## Deliverables

- `sim/scenarios/convoy_cruise.csv` — 5 cars, tight motorway convoy, 5 min;
  everything stays LIVE; exercises chip sorting and auto-zoom both ways.
- `sim/scenarios/split_rejoin.csv` — 3 cars; U2 takes a detour beyond
  radio range for 3 min (dot must walk LIVE→STALE→GHOST), then rejoins
  (GHOST→LIVE); include a stretch where U1 sits between U0 and U2 so the
  hop-1 relay path keeps U2 visible longer than direct range would.
- `sim/scenarios/no_fix_start.csv` — POV unit has no fix for the first
  60 s (`docs/06` NO FIX state), neighbours already beaconing.
- `sim/src/checks.c/.h` — `--check` mode: after a headless run, assert
  scripted expectations and exit non-zero on failure.
- Extend `sim/Makefile smoke` to also run
  `split_rejoin.csv --check` and `no_fix_start.csv --check`.

## `--check` expectations (hard-code per scenario in checks.c)

- `split_rejoin`: U2's tier sequence as observed at POV
  contains LIVE→STALE→GHOST→…→LIVE in order, with the relay window
  keeping U2 LIVE for ≥ 60 s after direct range is exceeded.
- `no_fix_start`: for the first 60 s no neighbour dot pixels are painted
  inside the radar area (sample the buffer), chips still present; after
  fix, dots appear within one beacon period.
- `convoy_cruise`: auto-zoom never leaves {250,500,1000} and changes
  ≤ 6 times total (hysteresis sanity).

## Acceptance — CI

`make -C sim smoke` (now including the two `--check` runs) green.

## Out of scope

New renderer features — if a check needs one, flag the task as blocked
instead of extending `radar_render` here.
