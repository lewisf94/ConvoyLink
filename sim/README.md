# ConvoyLink desktop radar simulator

Runs the real `convoy_geo` / `neighbor_table` / `radar_render` pipeline —
the exact same C code the firmware links — against scripted GPS tracks, so
UI and radar-logic changes can be seen and reviewed with no hardware.

## Build & run

```sh
make -C sim                                        # or: cd sim && make
./sim/build/convoysim sim/scenarios/overtake.csv --pov 0            # windowed
./sim/build/convoysim sim/scenarios/overtake.csv --pov 0 --dump out/ --headless  # BMP dump
make -C sim smoke                                   # build + dump + sanity-check
```

Needs `libsdl2-dev` to build (only the windowed path links against it at
runtime — `--headless` never touches SDL).

## CLI

```
convoysim <scenario.csv> [--pov N] [--speed X] [--range M]
          [--loss P] [--dump DIR] [--headless] [--seed S]
```

| Flag | Default | Meaning |
|---|---|---|
| `--pov N` | 0 | Which unit (0–4) you're viewing the radar as |
| `--speed X` | 1.0 | Playback speed multiplier (windowed mode only) |
| `--range M` | 800 | Direct beacon range in metres (simplified radio model) |
| `--loss P` | 0 | Per-hop packet loss probability, 0.0–1.0 |
| `--dump DIR` | — | Write `frame_NNNN.bmp` each render into DIR |
| `--headless` | off | No window; runs to the end of the scenario as fast as possible. Also triggered by `SDL_VIDEODRIVER=dummy` in the environment |
| `--seed S` | 1 | Seeds the deterministic loss-roll PRNG — same seed + scenario always produces the same delivered/dropped beacons |

## Keys (windowed mode)

| Key | Action |
|---|---|
| `0`–`4` | Switch point-of-view unit |
| `z` | Cycle zoom (auto → 250 m → 500 m → 1 km → 2 km → 4 km → auto) |
| `space` | Pause / resume |
| `←` / `→` | Scrub ±5 s |
| `q` / window close | Quit |

## Scenario CSV format

```
t_ms,unit_id,initials,lat,lon,speed_mps,course_deg
0,0,LF,51.500000,-0.120000,25.0,0.0
180000,0,LF,51.540468,-0.120000,25.0,0.0
```

Header row required. Each row is one **waypoint**; a unit can have any
number of waypoints, which must be in non-decreasing `t_ms` order. Position
is linearly interpolated between a unit's consecutive waypoints (speed and
course too — course interpolation doesn't handle wrapping through
0°/360°, so avoid crossing that boundary mid-scenario). Before a unit's
first waypoint it is **absent** (no fix, no beacons — this is how
`no_fix_start.csv` demonstrates the NO FIX state); after its last waypoint
it **parks** there indefinitely (speed forced to 0, still beaconing).

## Beacon/radio model

Every unit beacons every `CL_BEACON_PERIOD_MS` (5 s) starting from its
first waypoint. For each (sender, receiver) pair at each beacon tick:
direct delivery succeeds if their distance is within `--range` and the
loss roll passes; otherwise, if a third present unit is within `--range`
of *both* sender and receiver, a single-hop relay copy is attempted (its
own independent loss roll) — a simplified version of the real single-hop
relay in `docs/03-radio-protocol.md`.

State is never stepped incrementally: every render **fully replays** every
beacon event from t=0 up to the requested sim time into fresh neighbour
tables. This is what makes the `←`/`→` scrub keys trivially correct (no
"undo" needed) and is computationally cheap at this scenario scale.

## Bundled scenarios

| File | Demonstrates |
|---|---|
| `overtake.csv` | Three cars on a straight road; one overtakes and settles ahead, one starts out of range and catches up |
