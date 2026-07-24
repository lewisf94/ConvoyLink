# ConvoyLink Roadmap

Tasks live in `tasks/` (one file each, execution protocol in
`tasks/README.md`, live state in `tasks/STATUS.md`). Milestones gate on
either **CI** (software) or a **hardware checklist** (user with real parts).

## M0 — Scaffold ✅ (this repo state)

Docs, task queue, CI, `convoy_proto` exemplar component + host tests.

## M1 — Pure logic libraries ✅ (host-tested, no hardware)

> T01 convoy_geo · T02 adpcm codec · T03 nmea · T04 neighbor_table ·
> T05 radar_render primitives · T06 radar screen composition

Everything here compiles with plain gcc and is fully unit-tested. This is
the safest bulk work — do it first and strictly in order of dependencies
(T05 → T06; others independent). T18 `voice_pipe` is also pure-C and can be
pulled forward here once T02 is done.

**Gate:** CI green; geo precision, codec reseed, and renderer
strip-invariance tests pass. **Met** — 51/51 host tests passing.

## M2 — Desktop simulator

> T07 sim runner (SDL2) · T08 scenarios + headless PNG dump

Runs the real M1 code against scripted GPS tracks. From here on, UI changes
are reviewed as sim screenshots. Independent of M3 — can interleave.

**Gate:** `convoysim overtake.csv --dump` produces frames showing 3 cars,
correct bearings/distances (asserted in the scenario smoke test).

## M3 — Drivers & bring-up apps (first hardware contact)

> T09 sx1262 driver · T10 bringup_radio · T11 gps_uart + bringup_gps ·
> T12 audio_io (I²S) · T13 bringup_audio · T14 ili9341_disp + bringup_display

Each bring-up app has a printed checklist for the project owner. Do T09/T10
first — the LoRa link is the foundation — then T12/T13 (I²S audio), then the
rest in any order.

**Gate (hardware):** two bench units exchange LoRa pings at ≈ 0 % loss
across the house with sane RSSI; the I²S mic meters and an ADPCM loopback
plays back intelligibly; display test pattern at 5 Hz+; GPS fix at a window.
LoRa walk-test range recorded in STATUS.md.

## M4 — Integrated radar firmware `v0.1` (no voice yet)

> T15 app skeleton (tasks/queues/NVS provisioning/console) ·
> T16 radio task + beacons + relay · T17 GPS + neighbour + radar UI

**Gate (field):** two units in two cars, 30-minute drive: dots track with
correct bearing/distance, staleness tiers behave when separating, both
units run the whole drive without reset. This is the first "take it
outside" moment — do it before starting voice.

## M5 — Voice `v0.5` (digital, ESP-NOW)

> T18 voice_pipe (framing + jitter + conceal, pure C) ·
> T19 ESP-NOW transport + voice_task + UI

**Gate (field):** PTT voice intelligible between two cars in a tight convoy
(licence-free ESP-NOW, ~150–400 m); talker's initials show on the radar;
BUSY/ARMED_WAIT correct when both press; radar keeps updating while talking.

## M6 — Range upgrade + polish `v1.0`

> T20 UI polish (ghosts, off-scale arrows, zoom auto/hysteresis, night
> brightness) · T21 five-unit field guide + tuning pass ·
> T22 SX1262/Codec2 voice transport + A/B vs ESP-NOW (experimental)

**Gate:** the 5-car success criteria in `docs/00-brief.md`. T22 is optional
for v1.0 — ship on ESP-NOW if the field A/B isn't done; the winner of the
A/B becomes the documented default voice transport.

## Stretch (unscheduled)

> Heading-up radar, radar sweep animation, Wi-Fi HaLow (802.11ah) voice
> transport experiment, touch input (needs a pin re-plan), the analog SA818
> licensed variant as a third transport (`docs/04` appendix).

## Suggested execution notes

- M1+M2 are pure software — ideal for batch execution by a code agent.
- M3 tasks pair a *compile-checked* implementation with a *human* hardware
  session; write firmware first, then the owner runs the checklist and files
  findings back into the task file before it closes.
- Never start M4 before the M3 gate passes on real parts — integration bugs
  on unverified wiring waste everyone's time.
