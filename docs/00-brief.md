# ConvoyLink — Project Brief

GPS + voice communication units for groups road-tripping in separate cars.
Each car carries one self-contained, dash-mounted unit. The units form their
own radio network — **no mobile signal, no internet, no phones required**.

## What each unit does

1. **Push-to-talk voice** — hold the PTT button, talk, release. Everyone else
   hears you. Mono, speech-optimised, low latency, half-duplex (one talker at
   a time, like a walkie-talkie).
2. **Position beaconing** — broadcasts its own GPS position every 5 seconds.
3. **Radar display** — a 2.8" screen showing every other car as a labelled dot
   positioned relative to *your* car (always centre-screen), with range rings,
   a compass, distance readouts, and per-car freshness (live / stale / lost).
4. **Car power** — runs from the car's 12 V accessory line through a buck
   converter; powers on with ignition, no on/off button needed.

Up to **5 units** per convoy (unit IDs 0–4). Each shows a 2-letter label
(driver initials) on everyone else's radar.

## Locked decisions (agreed with the project owner, 2026-07-14)

These were explicitly chosen — do not revisit them in implementation tasks.

| Decision | Choice | Notes |
|---|---|---|
| Radio link | **NRF24L01+ PA/LNA only** — one radio carries both voice and GPS | RF24Audio library is AVR-only, so voice streaming is custom code per `docs/03-radio-protocol.md` and `docs/04-audio.md` |
| Toolchain | **ESP-IDF native** (v5.3.x, CMake, `idf.py`) | Not Arduino, not PlatformIO |
| Target MCU | Classic **ESP32** (WROOM-32 dev board, 30-pin, USB-C) | Needs the built-in DAC — do not port to S3/C3 (no DAC) |
| Scope extras | Module bring-up apps, host radar simulator, GitHub Actions CI | Enclosure CAD explicitly descoped |
| Desired range | Owner wants "several km / out of sight" | See range reality below — this is **not fully achievable** on this hardware and the mitigations are part of the design |

## Range reality (read before promising anything)

NRF24L01+ PA/LNA at 250 kbps with the stock rubber-duck antennas, mounted
*inside* vehicles, realistically achieves **~300 m – 1 km** car-to-car on open
road, less in towns or hilly terrain. Cheap clone modules often do worse.
The "several km" goal is addressed by *degrading gracefully*, not by pretending
the link reaches further:

1. **GPS relay** — every unit rebroadcasts other cars' position beacons once
   (single hop). A car in the middle of a strung-out convoy roughly doubles
   the radar's effective range. Voice is *not* relayed (bandwidth/latency).
2. **Ghost dots** — when a car's beacons stop arriving, its dot greys out and
   shows "last seen" age instead of vanishing, so you still know where to
   backtrack to. (Supersedes the earlier "drop after 15 s" idea.)
3. **Antenna placement guidance** — documented in `docs/02-hardware.md`;
   window-mounted antennas buy real range.
4. **Documented upgrade path** (future, out of scope now): SA818 VHF/UHF
   module for multi-km voice, or LoRa (SX1276/8) for multi-km GPS telemetry.
   The packet formats in `docs/03-radio-protocol.md` are transport-agnostic
   on purpose.

## Owned hardware (design around exactly these)

| Role | Module owned |
|---|---|
| MCU | ESP32 dev board, USB-C, 30-pin (classic ESP32-WROOM-32) |
| GPS | GY-NEO6MV2 (u-blox NEO-6M, UART, 9600 baud default) |
| Radio | NRF24L01+ **PA/LNA** with external SMA antenna |
| Mic | MAX9814 electret mic amp (AGC, 1.25 V output bias) |
| Speaker amp | PAM8403 class-D 3 W stereo board (used mono) |
| Speaker | AIYIMA 40 mm 4 Ω 3 W full-range |
| Display | 2.8" SPI TFT 240×320, ILI9341, with XPT2046 resistive touch |
| Power | Mini MP1584EN 12 V→adjustable buck converters (several) |
| Extras | Schottky diodes, assorted capacitors, push buttons |
| Printer | Ender 3 V2 + PLA+ (enclosure descoped, but keep mounting holes sane) |

## Success criteria for v1.0

- Two units on a bench exchange beacons and voice reliably at 30 m.
- Five units in cars: radar shows all cars with correct relative bearing and
  distance, updating ≤ 5 s, out to at least 300 m direct / 600 m relayed.
- PTT voice is intelligible at highway noise levels, end-to-end latency
  under 250 ms, no lockups over a 2-hour drive.
- A unit power-cycles with ignition and is on the radar within 60 s of
  GPS cold-start (typically much faster warm).

## Document map

| Doc | Contents |
|---|---|
| `01-architecture.md` | Firmware structure, FreeRTOS tasks, dataflow, memory budget |
| `02-hardware.md` | BOM, full pin map, wiring tables, power tree |
| `03-radio-protocol.md` | NRF24 configuration, packet formats, airtime budget, relay |
| `04-audio.md` | Audio pipeline, ADPCM codec, jitter buffer, PTT state machine |
| `05-gps-geo.md` | NMEA parsing, geodesy math, neighbour table, staleness tiers |
| `06-ui.md` | Radar screen spec — exact layout, colours, states |
| `07-dev-guide.md` | ESP-IDF setup, build/flash/test commands, unit provisioning |
| `../ROADMAP.md` | Milestones and task ordering |
| `../tasks/` | Self-contained implementation tasks (the "bulk build" queue) |
