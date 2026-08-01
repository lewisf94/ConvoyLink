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

## Tasks

Names, cores and priorities are transcribed from `docs/01`'s table and
live in `app_tasks.h`:

| Task | Core | Prio | T15 state |
|---|---|---|---|
| `radio_task` | 1 | 12 | Drains `tx_q`, drains RX, retries a failed radio init every 5 s |
| `voice_task` | 1 | 8 | Idle heartbeat (T18/T19) |
| `gps_task` | 0 | 6 | Publishes real fixes to state; beacons are T16 |
| `ctrl_task` | 0 | 5 | Real 50 ms button debounce into `ctrl_q` |
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
