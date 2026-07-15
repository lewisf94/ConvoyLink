# ConvoyLink — Project Brief

GPS + voice communication units for groups road-tripping in separate cars.
Each car carries one self-contained, dash-mounted unit. The units form their
own radio network — **no mobile signal, no internet, no phones required**.

## What each unit does

1. **Push-to-talk voice** — hold the PTT button, talk, release. Everyone else
   hears you. Analog UHF FM (walkie-talkie grade), multi-km range, half-duplex.
2. **Position beaconing** — broadcasts its own GPS position every 5 seconds
   over a LoRa data link with multi-km range.
3. **Radar display** — a 2.8" screen showing every other car as a labelled dot
   positioned relative to *your* car (always centre-screen), with range rings,
   a compass, distance readouts, and per-car freshness (live / stale / ghost).
4. **Car power** — runs from the car's 12 V accessory line through a buck
   converter; powers on with ignition, no on/off button needed.

Up to **5 units** per convoy (unit IDs 0–4). Each shows a 2-letter label
(driver initials) on everyone else's radar.

## Locked decisions

### v1 decisions (2026-07-14)

| Decision | Choice | Notes |
|---|---|---|
| Toolchain | **ESP-IDF native** (v5.3.x, CMake, `idf.py`) | Not Arduino, not PlatformIO |
| Target MCU | Classic **ESP32** (WROOM-32 dev board, 30-pin, USB-C) | |
| Scope extras | Module bring-up apps, host radar simulator, GitHub Actions CI | Enclosure CAD explicitly descoped |

### v2 radio architecture (2026-07-15, after the range/reliability study)

The v1 plan carried everything over one NRF24L01+ (300 m–1 km realistic).
The owner asked for a researched "solid, good-range" setup; the study
concluded and the owner accepted:

| Decision | Choice | Why |
|---|---|---|
| Position link | **LoRa, Semtech SX1262** (EBYTE E22-900M22S, +22 dBm) | The exact radio Meshtastic proves at 1–3 mi urban / 3–5 mi rural car-to-car; one SKU tunes 850–930 MHz (covers EU 868 + US/AU 915); 5 s beacons ≈ 1.2 % duty — legal everywhere; mature ESP-IDF driver exists |
| Voice link | **Analog UHF FM, NiceRF SA818S-U** (400–480 MHz, 1 W/0.5 W) | A complete walkie-talkie on a module: ESP32 keys PTT + configures channel/CTCSS over UART, mic/speaker wire to it directly. Multi-km, and it deletes the v1 plan's riskiest firmware (half-duplex DMA audio, ADPCM codec, jitter buffer) |
| NRF24L01+ | **Retired** | Outclassed on both jobs; modules kept as spares/experiments |
| Region | **Runtime config, not compile-time** (NVS: `region` + voice channel) | Same hardware everywhere; frequency plans + legal notes in `docs/03`/`docs/04` |
| Voice metadata | None — analog FM carries no sender ID | Radar/UI shows a generic RX indicator, not talker initials (see docs/06) |

Digital voice over the LoRa radio (Codec2, as in sh123/esp32_loradv and
LILYGO's T3-S3 MVSR kit) was evaluated and rejected for v2: legally marginal
for sustained talking under EU duty-cycle rules, experimental-grade, and the
chosen ESP-IDF SX126x driver lacks FSK support. Integrated boards (LILYGO
T-TWR Plus, T-Deck Plus) were evaluated and set aside in favour of the
discrete build on owned parts. All noted as future experiments only.

## Range expectations (post-v2, still honest)

- **Radar (LoRa beacons):** 2–5 km car-to-car typical with window-mounted
  antennas, more line-of-sight, less in dense terrain. Single-hop relay
  through middle cars (`docs/03`) roughly doubles coverage along a
  strung-out convoy.
- **Voice (SA818, 1 W UHF):** 1–5 km typical mobile-to-mobile; terrain
  dependent, degrades gracefully (static, not silence).
- **Ghost dots**: when a car's beacons stop, its dot greys out with a
  "last seen" age instead of vanishing — you always know where to backtrack.
- Antenna placement guidance in `docs/02` matters more than any firmware.

## Owned hardware (design around these) + v2 additions

| Role | Module | Status |
|---|---|---|
| MCU | ESP32 DevKit 30-pin USB-C (WROOM-32) | owned |
| GPS | GY-NEO6MV2 (u-blox NEO-6M) | owned |
| Display | 2.8" ILI9341 SPI 240×320 (+XPT2046 touch, unused) | owned |
| Mic | MAX9814 electret amp | owned |
| Speaker amp + speaker | PAM8403 + AIYIMA 40 mm 4 Ω | owned |
| Power | Mini MP1584EN bucks ×2, Schottky, caps | owned |
| **Position radio** | **EBYTE E22-900M22S (SX1262) + 868/915 whip** | **buy, ~£7–10/unit** |
| **Voice radio** | **NiceRF SA818S-U + UHF whip antenna** | **buy, ~£12–18/unit** |
| (retired) | NRF24L01+ PA/LNA | spare drawer |

## Success criteria for v1.0

- Two units on a bench exchange beacons and voice reliably at 30 m.
- Five units in cars: radar shows all cars with correct relative bearing and
  distance, updating ≤ 5 s, out to at least **2 km direct / 4 km relayed**.
- PTT voice is intelligible at highway noise levels **at ≥ 1 km**, and
  usable (increasing static) beyond.
- A unit power-cycles with ignition and is on the radar within 60 s of
  GPS cold-start (typically much faster warm).
- No lockups or resets over a 2-hour drive, any unit.

## Document map

| Doc | Contents |
|---|---|
| `01-architecture.md` | Firmware structure, FreeRTOS tasks, dataflow, memory budget |
| `02-hardware.md` | BOM, full pin map, wiring tables, power tree, antennas |
| `03-radio-protocol.md` | LoRa link config, packet formats, duty budget, relay |
| `04-voice-sa818.md` | SA818 integration: wiring, UART control, PTT, channels & legality |
| `05-gps-geo.md` | NMEA parsing, geodesy math, neighbour table, staleness |
| `06-ui.md` | Radar screen spec — exact layout, colours, states |
| `07-dev-guide.md` | ESP-IDF setup, build/flash/test commands, provisioning |
| `../ROADMAP.md` | Milestones and task ordering |
| `../tasks/` | Self-contained implementation tasks (the "bulk build" queue) |
