# ConvoyLink Roadmap

Tasks live in `tasks/` (one file each, execution protocol in
`tasks/README.md`, live state in `tasks/STATUS.md`). Milestones gate on
either **CI** (software) or a **hardware checklist** (user with real parts).

## M0 — Scaffold ✅ (this repo state)

Docs, task queue, CI, `convoy_proto` exemplar component + host tests.

## M1 — Pure logic libraries (host-tested, no hardware)

> T01 convoy_geo · T02 adpcm · T03 nmea · T04 neighbor_table ·
> T05 radar_render primitives · T06 radar screen composition

Everything here compiles with plain gcc and is fully unit-tested. This is
the safest bulk work — do it first and strictly in order of dependencies
(T05 → T06; others independent).

**Gate:** CI green; codec round-trip SNR and geo precision tests pass.

## M2 — Desktop simulator

> T07 sim runner (SDL2) · T08 scenarios + headless PNG dump

Runs the real M1 code against scripted GPS tracks. From here on, UI changes
are reviewed as sim screenshots. Independent of M3 — can interleave.

**Gate:** `convoysim overtake.csv --dump` produces frames showing 3 cars,
correct bearings/distances (asserted in the scenario smoke test).

## M3 — Drivers & bring-up apps (first hardware contact)

> T09 nrf24 driver · T10 bringup_radio · T11 gps_uart + bringup_gps ·
> T12 audio_io · T13 bringup_audio · T14 ili9341_disp + bringup_display

Each bring-up app has a printed checklist for the project owner. Do T09/T10
first — radio is the foundation — then T12/T13 (the risky audio mode-switch),
then the rest in any order.

**Gate (hardware):** two bench units exchange pings at < 2 % loss across the
house; mic→speaker loopback intelligible; display test pattern at 5 Hz+;
GPS fix at a window.

## M4 — Integrated radar firmware `v0.1` (no voice yet)

> T15 app skeleton (tasks/queues/NVS provisioning/console) ·
> T16 radio task + beacons + relay · T17 GPS + neighbour + radar UI

**Gate (field):** two units in two cars, 30-minute drive: dots track with
correct bearing/distance, staleness tiers behave when separating, both
units run the whole drive without reset. This is the first "take it
outside" moment — do it before starting voice.

## M5 — Voice `v0.5`

> T18 PTT TX chain · T19 RX jitter buffer + playback

**Gate (field):** PTT voice intelligible between two moving cars at
≥ 300 m; latency subjectively "walkie-talkie instant" (< 250 ms); radar
keeps updating while talking.

## M6 — Polish `v1.0`

> T20 UI polish (ghosts, off-scale arrows, zoom auto/hysteresis, night
> brightness) · T21 five-unit field test guide + tuning pass

**Gate:** the 5-car success criteria in `docs/00-brief.md`.

## Stretch (unscheduled)

> T22 candidates: heading-up radar, sweep animation, touch input,
> AUX volume, SENSE_12V graceful shutdown, SA818/LoRa transport experiments.

## Suggested execution notes

- M1+M2 are pure software — ideal for batch execution by a code agent.
- M3 tasks pair a *compile-checked* implementation with a *human* hardware
  session; write firmware first, then the owner runs the checklist and files
  findings back into the task file before it closes.
- Never start M4 before the M3 gate passes on real parts — integration bugs
  on unverified wiring waste everyone's time.
