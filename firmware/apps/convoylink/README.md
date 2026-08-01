# convoylink — the real firmware

The integrated app all five units run. T15 builds its frame: FreeRTOS
task layout, shared state, queues, NVS identity and the console. Task
bodies are stubs that T16–T18 fill in — it boots, breathes, renders, and
transmits nothing yet.

## Build & flash

```sh
. ~/esp-idf/export.sh
idf.py -C firmware/apps/convoylink set-target esp32s3 build
idf.py -C firmware/apps/convoylink -p /dev/ttyUSB0 flash monitor
```

## Provisioning (once per device)

A fresh unit boots as `U? --` showing **PROVISION ME**, transmits nothing,
and its console still works:

```
convoy> unitcfg set 2 JD          # unit_id 2, initials "JD"
convoy> unitcfg region EU         # EU | US | AU — selects the LoRa frequency
convoy> unitcfg voice espnow      # espnow (default) | sx1262
convoy> unitcfg show
```

Settings are written to NVS immediately but apply on reboot. All five
units must share the same region **and** the same voice transport — mixing
them means they cannot hear each other (`docs/07`).

## Console

| Command | Action |
|---|---|
| `unitcfg …` | Provisioning, as above |
| `status` | Identity, peripheral health, current fix |
| `free` | Heap free / min-free / largest block, plus queue drop counts |
| `radiostat` | LoRa counters: tx/rx/invalid/relayed/suppressed/dropped |
| `nt` | Neighbour table: seq, age, tier, distance, direct/via_relay |

## Buttons

| Input | Action |
|---|---|
| AUX short press | Cycle zoom: auto → 250 m → 500 m → 1 km → 2 km → 4 km → auto |
| AUX 2 s hold | Cycle backlight: 100 % → 60 % → 25 % |
| PTT | Events produced and logged; no consumer until T18 |

## Tasks

Names, cores and priorities are transcribed from `docs/01`'s table and
live in `app_tasks.h`:

| Task | Core | Prio | T15 state |
|---|---|---|---|
| `radio_task` | 1 | 12 | Real: RX dispatch, single-hop relay with suppression, LBT |
| `voice_task` | 1 | 8 | Idle heartbeat (T18/T19) |
| `gps_task` | 0 | 6 | Real: publishes fixes, queues own beacon every 5 s ± jitter |
| `ctrl_task` | 0 | 5 | Real: 50 ms debounce, AUX short vs 2 s hold |
| `ui_task` | 0 | 4 | Real: renders the scene at 5 Hz through `rr_screen_draw` |

Set `log_level` to DEBUG to see all five heartbeats.

## Acceptance — hardware checklist (project owner)

- [ ] Fresh flash boots to the **PROVISION ME** screen. `unitcfg set 0 LF`,
      reboot → the status bar shows `U0 LF`.
- [ ] All five task heartbeats appear at DEBUG log level.
- [ ] The radar area renders the "waiting for convoy…" state at 5 Hz —
      watch for flicker, which would mean strip flushing is struggling.
- [ ] 10-minute idle soak: no watchdog resets, and `free` shows a stable
      heap high-water mark.

## Field checklist — the M4 gate (project owner)

Bench:

- [ ] Unit at a window with a fix: own marker centred, sats count sane.
- [ ] Second unit 50 m down the street: its dot appears in < 10 s, the
      distance is plausible, and the bearing points the right way — walk a
      square around the house and watch it track.
- [ ] AUX cycles zoom, ring labels change, auto mode returns sanely.
- [ ] AUX held 2 s cycles the backlight.

Field (two cars, 30-minute drive):

- [ ] The follower's dot tracks the leader with correct bearing through
      turns.
- [ ] Separate by ~1 km: the dot goes STALE → GHOST with the age counting
      up; reconverge and it returns to LIVE **without a reboot**.
- [ ] Both units run the whole drive with no reset or watchdog (check the
      logs).
- [ ] Record the real maximum range seen in `tasks/STATUS.md`.

**Before this drive**, read the reboot-`seq` finding at the bottom of
`tasks/T16-radio-task.md`. Power-cycling one unit mid-convoy can make it
invisible to the others for up to ~40 minutes — if you reboot a unit
during testing and it never reappears, that is the known cause, not a
wiring fault.
