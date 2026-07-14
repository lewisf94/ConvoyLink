# T07 — Desktop radar simulator (SDL2)

**Depends:** T06 · **Phase:** M2
**Required reading:** `docs/07-dev-guide.md` §Simulator; `docs/06-ui.md`

## Goal

Run the real pure-C stack (geo, neighbor_table, radar_render, convoy_proto)
on the desktop against scripted GPS tracks. This is how UI work gets
reviewed (screenshots) and how radar logic gets exercised end-to-end
without hardware.

## Deliverables

- `sim/Makefile` — targets: default build, `smoke`, `clean`
- `sim/src/main.c` (SDL window, main loop, key handling)
- `sim/src/scenario.c/.h` (CSV load + interpolation)
- `sim/src/frame_dump.c/.h` (BMP writer — no external image libs)
- `sim/scenarios/overtake.csv` (bundled 3-car scenario, see below)
- `sim/README.md` (usage, keys, scenario format)

## Behaviour

- CLI: `convoysim <scenario.csv> [--pov N] [--speed X] [--range M]
  [--loss P] [--dump DIR] [--headless] [--seed S]`
- Scenario CSV (`docs/07`): `t_ms,unit_id,initials,lat,lon,speed_mps,course_deg`
  — waypoints per unit; positions linearly interpolated between waypoints;
  before first/after last waypoint the unit is absent/parked.
- Sim clock ticks at 5 Hz renders (× `--speed`). Every `CL_BEACON_PERIOD_MS`
  of sim time, each non-POV unit emits a `cl_beacon_t` (built with
  `cl_make_beacon`) which is dropped when 3D distance to POV >
  `--range` (default 800 m) or with probability `--loss` (default 0,
  deterministic PRNG from `--seed`), else fed to `nt_update_from_beacon`.
  The POV unit's own fix comes straight from its track. **Relaying: a
  beacon dropped POV-side but within range of another unit that is itself
  in POV range gets a hop-1 copy delivered** (simplified single-hop model
  mirroring docs/03).
- Rendering: build `rr_scene_t` exactly as firmware will (nt_snapshot,
  zoom auto default), call `rr_screen_draw` into one 240×320 buffer,
  convert RGB565→RGB888, blit via SDL texture scaled ×2 (480×640 window).
- Keys: `0–4` switch POV, `z` cycle zoom, `space` pause, `←/→` ±5 s scrub,
  `q` quit.
- `--dump DIR`: write frame_%04d.bmp (24-bit BMP) each render;
  `--headless`: no window (`SDL_VIDEODRIVER=dummy` also honoured), run
  scenario to end at max speed, then exit 0.
- `smoke` Makefile target: build, then
  `./build/convoysim scenarios/overtake.csv --pov 0 --headless --dump build/frames`
  and assert ≥ 10 BMPs of the exact expected byte size exist.

## `overtake.csv` (author it with these beats)

Three cars northbound on a straight road near lat 51.50, lon −0.12:
U0 "LF" steady 25 m/s; U1 "AM" starts 300 m behind, does 32 m/s until 200 m
ahead of U0, then matches speed; U2 "RK" starts 900 m behind (out of 800 m
range → enters as new contact, demonstrates STALE→LIVE transition), does
30 m/s. Duration 180 s.

## Acceptance — CI

`make -C sim && make -C sim smoke` green locally; CI's `sim` job stops
skipping automatically once `sim/Makefile` exists. Host tests stay green.

## Out of scope

Voice/audio anything, PNG encoding, fancy UI chrome around the screen
(a plain scaled blit is fine), scenario editor.
