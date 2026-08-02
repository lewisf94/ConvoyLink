# convoylink — the real firmware

The integrated app all five units run: FreeRTOS task layout, shared
state, queues, NVS identity/console (T15), real beacons/relay (T16), real
radar (T17), real voice (T18/T19), and the road-trip-ready polish —
boot splash, night mode, watchdog coverage, fault tiles (T20).

## Build & flash

```sh
. ~/esp-idf/export.sh
idf.py -C firmware/apps/convoylink set-target esp32s3 build
idf.py -C firmware/apps/convoylink -p /dev/ttyUSB0 flash monitor
```

## Boot sequence

Every boot shows a 1.5 s splash — `ConvoyLink`, your identity (or
`UNPROVISIONED`), and the firmware version (`git describe`, embedded by
the build system — no separate versioning step needed) — before the radar
takes over. If the previous boot ended in a watchdog reset, a banner
about it prints to the log first, before anything else.

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
| `voice` | Transport, PTT state, tx-seconds, frame counters (in/out/dropped/**invalid**) |
| `crash` | **Debug only** — deliberately hangs `ui_task` to test the watchdog + reset-reason banner on demand, without needing a real fault |

## Buttons

| Input | Action |
|---|---|
| AUX short press | Cycle zoom: auto → 250 m → 500 m → 1 km → 2 km → 4 km → auto |
| AUX 2 s hold | Cycle backlight: 100 % → 60 % → 25 %, **persisted to NVS** and re-applied on every boot (no auto-dim — v1 remembers your last choice only) |
| PTT hold | Talk. Busy channel → BUSY, and you get it when the other unit releases |

## Tasks

Names, cores and priorities are transcribed from `docs/01`'s table and
live in `app_tasks.h`:

| Task | Core | Prio | Status |
|---|---|---|---|
| `radio_task` | 1 | 12 | Real: RX dispatch, single-hop relay with suppression, LBT |
| `voice_task` | 1 | 8 | Real: PTT state machine, ESP-NOW transport, jitter playback |
| `gps_task` | 0 | 6 | Real: publishes fixes, queues own beacon every 5 s ± jitter |
| `ctrl_task` | 0 | 5 | Real: 50 ms debounce, AUX short vs 2 s hold |
| `ui_task` | 0 | 4 | Real: renders the scene at 5 Hz through `rr_screen_draw` |

Set `log_level` to DEBUG to see all five heartbeats. `radio_task`,
`voice_task` and `ui_task` are subscribed to the 10 s task watchdog
(`sdkconfig.defaults`); a task that stops looping trips it, resets the
unit, and the reset-reason banner above tells you which kind of watchdog
fired on the next boot. `gps_task` and `ctrl_task` aren't subscribed —
both only ever block on bounded reads/timeouts, so a hang there would
mean the underlying driver itself has stopped responding, which is a
different failure to chase than "this task's own loop got stuck".

## Fault tiles (docs/01 §Error-handling)

A failed peripheral never halts boot or disturbs the radar — it shows a
red tile instead, and the owning task keeps retrying every 5 s:

| Tile | Where | Meaning |
|---|---|---|
| `RADIO?` | Status bar centre, blinking (same slot/cadence as `NO FIX`) | SX1262 init failed — positions and relay are down; retries every 5 s |
| `VOICE?` | Status bar right (replaces TX/RX — a faulted transport can't do either) | `audio_io` or the voice transport failed to init; retries every 5 s |
| Sats shown **red**, not green | Status bar centre | GPS UART has produced **no byte at all** for > 10 s — the module itself looks dead, distinct from an ordinary "no fix yet" (which still shows `NO FIX`, not red sats) |

`docs/06` predates these three states and doesn't lay out their exact
pixel geometry; the placements above were chosen to reuse existing
status-bar patterns rather than invent new ones (see the comment at the
top of `radar_scene.h`).

## Acceptance — hardware checklist (project owner)

- [ ] Fresh flash boots: splash (`ConvoyLink`, identity, version) for
      ~1.5 s, then the **PROVISION ME** screen. `unitcfg set 0 LF`,
      reboot → splash shows `U0 LF`, then the status bar does too.
- [ ] Cold boot to a usable radar in < 5 s, excluding GPS lock.
- [ ] All five task heartbeats appear at DEBUG log level.
- [ ] The radar area renders the "waiting for convoy…" state at 5 Hz —
      watch for flicker, which would mean strip flushing is struggling.
- [ ] Run `crash`: expect a reset within ~10 s, then a watchdog
      reset-reason banner at the top of the next boot's log.
- [ ] Disconnect the SX1262 mid-run (or short/remove its NSS or BUSY
      line): `RADIO?` appears, the unit keeps rendering, and reconnecting
      recovers within 5 s with **no reboot**.
- [ ] Unplug the I²S mic or amp: `VOICE?` appears, radar unaffected,
      recovers the same way once replugged.
- [ ] Cover the GPS antenna or unplug it: sats turn **red** after ~10 s of
      silence (not simply `NO FIX`, which is the no-fix-yet state).
- [ ] Set a night level, power-cycle: it comes back exactly as left.
- [ ] 2-hour bench soak with a second unit beaconing and periodic PTT:
      zero resets, `radiostat`/`free` drop counters ≈ 0, heap stable.

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

Power-cycling a unit mid-convoy is safe to test: it reappears on the
others' radar within ~15 s (`docs/05` §Sequence resync). Before that fix
it could stay invisible for ~40 minutes — background at the bottom of
`tasks/T16-radio-task.md`.

## Voice

Hold PTT, talk, and every unit in ESP-NOW range hears you with your
initials on their status bar. 8 kHz mono, IMA ADPCM, 20 ms frames.

The transport is chosen at boot from NVS (`unitcfg voice`). All units must
match — `espnow` and `sx1262` cannot hear each other. Selecting `sx1262`
today gives a `VOICE?` fault rather than silently falling back to
ESP-NOW, because a quiet downgrade would look exactly like a broken radio;
that transport arrives in T22.

Wi-Fi comes up **only** for ESP-NOW: STA mode, fixed channel
`CL_ESPNOW_CHANNEL`, broadcast peer. The firmware never associates, never
hosts, never scans, and never enables Bluetooth (`docs/04` §invariant).
It costs ~51 KB of RAM, measured — close to docs/01's ~50 KB estimate —
leaving ~198 KB free.

Voice and positions use different radios, so the radar keeps updating
while you talk.

### Field checklist — the M5 gate (project owner, 2 units)

- [ ] Two units on the same `voice` transport, 5 m apart: PTT conversation
      both ways, intelligible, no lockups over 20 exchanges.
- [ ] The receiver's status bar shows the talker's initials, and clears
      within 1 s of release.
- [ ] Both press at once: the later presser sees **BUSY** and gets the
      channel when the first releases.
- [ ] Clap test: latency subjectively instant (< ~¼ s). Note it in
      `tasks/STATUS.md`.
- [ ] The radar keeps updating through a 30 s monologue.
- [ ] Range walk: record where ESP-NOW voice breaks up in
      `tasks/STATUS.md`. This calibrates the ~150–400 m estimate and is
      what justifies (or doesn't) the T22 SX1262/Codec2 transport.
