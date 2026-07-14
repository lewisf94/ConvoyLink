# T17 — Radar integration: the v0.1 field gate

**Depends:** T06, T11, T16 · **Phase:** M4 — closing this task = milestone
M4, the first in-car version.
**Required reading:** `docs/06-ui.md`; `docs/05` §Own-position edge cases;
`ROADMAP.md` §M4

## Goal

Wire the real pipeline into the screen: live neighbour table + own fix →
`rr_scene_t` → strips → panel, with buttons. After this task, two units in
two cars show each other on radar.

## Deliverables (within `firmware/apps/convoylink/main/`)

- `ui_task.c` — real implementation at 5 Hz: state snapshot →
  `nt_snapshot` → build `rr_scene_t` (own fix/course rules from docs/05,
  zoom resolution below) → render 16 × 240×20 strips (double-buffered)
  → `disp_flush`
- Zoom logic: mode in state (default `RR_ZOOM_AUTO`); auto scale via
  `rr_pick_zoom` over LIVE+STALE distances with docs/06 hysteresis
  (change only past 90 % boundary, ≥ 2 s apart); AUX short-press cycles
  modes via ctrl_task event
- `ctrl_task.c` — real implementation: PTT/AUX debounce (50 ms), AUX
  short vs 2 s hold (hold → backlight cycle 100/60/25 %), events to a
  small `ctrl_evt` queue consumed by ui/audio tasks. PTT events are
  produced but consumed by nothing until T18 (log them)
- Own-position marker uses live GPS fix (1 Hz) between beacons; course
  arrow validity per `CL_COURSE_VALID_DM_S`

## Acceptance — CI

`./tools/ci_build_apps.sh` + host tests + sim smoke all green (no sim or
renderer changes expected — if the integration reveals a renderer bug, fix
it under T06 rules: with a host test reproducing it first).

## Acceptance — hardware (owner checklist == M4 gate)

Bench: 
- [ ] Unit at a window with fix: own marker centred, sats count OK
- [ ] Second unit 50 m down the street: dot appears < 10 s, distance
      plausible, bearing points the right way (walk a square around the
      house and watch it track)
- [ ] AUX cycles zoom; ring labels change; auto mode returns sanely

Field (two cars, 30 min drive):
- [ ] Follower's dot tracks leader with correct bearing through turns
- [ ] Separate by ~1 km: dot goes STALE→GHOST with age counting up;
      reconverge: back to LIVE without a reboot
- [ ] Both units run the full drive without reset/watchdog (check logs)
- [ ] Record real max range seen in STATUS.md

## Out of scope

Voice (T18/T19), night-mode polish and off-scale arrow tuning beyond what
T06 already renders (T20).
