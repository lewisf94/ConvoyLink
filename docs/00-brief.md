# ConvoyLink — Project Brief

GPS + voice communication units for groups road-tripping in separate cars.
Each car carries one self-contained, dash-mounted unit. The units form their
own radio network — **no mobile signal, no internet, no phones required**.

## What each unit does

1. **Push-to-talk voice** — hold the PTT button, talk, release. Everyone else
   hears you. Digital, half-duplex, speech-grade; carried licence-free over
   ESP-NOW (default) or the SX1262/Codec2 link (`docs/04`).
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
| Target MCU | Classic **ESP32** (WROOM-32 dev board, 30-pin, USB-C) | *Superseded by v2.1 → ESP32-S3, below* |
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

### v2.1 platform + voice-integration (2026-07-16)

| Decision | Choice | Why |
|---|---|---|
| Target MCU | **ESP32-S3-DevKitC-1 (N16R8)** — supersedes the classic WROOM-32 | Voice went analog (no on-chip DAC needed), which was the only thing tying us to the classic part. S3 gives more RAM (8 MB PSRAM), native USB-JTAG/serial, more usable GPIO, faster CPU. ESP-IDF v5.3 targets it directly (`set-target esp32s3`) |
| Voice packaging | **Integrated in the one unit** (confirmed with owner) | The device is deliberately all-in-one: GPS radar **and** push-to-talk voice in a single dash box. Separate handhelds were considered and rejected — the whole point is one device |
| Voice legality (UK) | ~~SA818 on PMR446, 0.5 W~~ | **Superseded by v3, below.** This row's reasoning contained an error corrected in v3: it conflated "the SA818 arrangement isn't licence-exempt" with "there's no licence-exempt way to do voice at all." |

If a specific S3 board other than the DevKitC-1 is used, only the pin map
in `docs/02` changes — nothing structural.

### v3 voice architecture — digital, transport-abstracted (2026-07-16)

Correcting v2.1: calling the SA818-on-PMR446 plan a "grey area" was wrong.
PMR446's licence exemption is conditional on the **equipment class** —
hand-portable, **integral non-removable antenna**, ≤500 mW ERP, conformity
to **EN 303 405**. An SA818 with an external/removable antenna fails those
conditions by construction, so it would be *non-compliant licence-exempt
operation*, not a grey area. (A CE-RED-marked SA818 is only a certified
*component*; it doesn't make the finished device compliant PMR446 kit.)

The real conclusion is the opposite of v2.1's: **licence-free all-in-one
voice IS possible** — just not by bolting an antenna onto an SA818. The
route is to keep the emission *inside an exemption's technical envelope*,
which is the same legal basis our LoRa beacons already rely on:

| Decision | Choice | Why |
|---|---|---|
| Voice = **digital, transport-abstracted** | One audio pipeline (I²S mic → codec → frames → jitter buffer → I²S amp); the transport is swappable | The pipeline is identical whatever carries the frames, so we don't have to bet on one radio — build the pipeline once, A/B the transports in the field |
| **Ship ESP-NOW first** | PTT voice over the S3's own 2.4 GHz radio (ESP-NOW), ADPCM | Licence-free under the ≤100 mW EIRP wideband-data exemption; **zero new RF and no new parts** (radio's already on the board); validates the whole audio chain with no RF unknowns. Also architecturally clean — voice on Wi-Fi radio, positions on the SX1262, two independent radios, simultaneous |
| **SX1262 / Codec2 as the range upgrade** | Half-duplex Codec2-3200 over GFSK at **869.7–870 MHz, 5 mW ERP** (licence-free, no duty-cycle limit on that sub-band) | ~9 dB sub-GHz propagation edge → roughly 2.5–3× the ESP-NOW range. Follow-on experiment: needs an FSK driver, Codec2, and shared-radio arbitration with the LoRa beacons — so it comes *after* the pipeline is proven on ESP-NOW |
| **SA818 parked** | Kept as a documented **licensed variant** (ham 70 cm, 1 W), not in the core build | Fully legal *only* if every talker holds a Foundation licence. Best range/quality by far — the right answer for a licensed group, wrong default for a no-licence friends' build |
| Talker identity | **Back** — digital frames carry the sender uid | Analog FM couldn't; digital can, so the radar shows *who* is talking again (`docs/06`) |

Honest expectation: **both licence-free routes give up multi-km voice.**
The radar (LoRa) stays the km-scale awareness layer; voice becomes the
"roughly within sight" channel — which is when you actually talk. A fully
type-approved integral-antenna PMR446 build is *theoretically* licence-free
too, but needs EN 303 405 lab testing — disproportionate for a hobby run.
Full detail and the corrected legality table live in `docs/04-voice.md`.

## Range expectations (v3, honest)

- **Radar (LoRa beacons):** 2–5 km car-to-car typical with window-mounted
  antennas, more line-of-sight, less in dense terrain. Single-hop relay
  through middle cars (`docs/03`) roughly doubles coverage along a
  strung-out convoy. **This is the km-scale awareness layer.**
- **Voice — ESP-NOW (ships first):** ~150–400 m car-to-car (2.4 GHz,
  100 mW EIRP). Fine for a tight convoy; drops out when cars string out.
- **Voice — SX1262/Codec2 (range upgrade):** ~500 m–1.5 km (869.7 MHz,
  5 mW ERP, sub-GHz propagation). The field A/B decides the default.
- **Voice — SA818 licensed variant:** 1–5 km, but needs ham licences.
- **Ghost dots**: when a car's beacons stop, its dot greys out with a
  "last seen" age instead of vanishing — you always know where to backtrack.
  This is why the radar, not voice, is the primary "where is everyone" tool.
- Antenna placement guidance in `docs/02` matters more than any firmware.

## Hardware (design around these)

| Role | Module | Status |
|---|---|---|
| MCU | **ESP32-S3-DevKitC-1 (N16R8)** — 16 MB flash, 8 MB PSRAM, 44-pin | **buy, ~£8–12/unit** (classic WROOM-32 retired to the spare drawer) |
| GPS | GY-NEO6MV2 (u-blox NEO-6M) | owned |
| Display | 2.8" ILI9341 SPI 240×320 (+XPT2046 touch, unused) | owned |
| **Mic** | **INMP441 I²S MEMS mic** | **buy, ~£2/unit** (MAX9814 → spare drawer) |
| **Speaker amp** | **MAX98357A I²S class-D** (drives the 4 Ω speaker directly) | **buy, ~£2/unit** (PAM8403 → spare drawer) |
| Speaker | AIYIMA 40 mm 4 Ω 3 W | owned |
| Power | Mini MP1584EN bucks ×2, Schottky, caps | owned |
| **Position + voice radio** | **EBYTE E22-900M22S (SX1262) + 868/915 whip** | **buy, ~£7–10/unit** (carries LoRa beacons; also the Codec2 voice transport) |
| Voice (ESP-NOW) | none — the S3's built-in 2.4 GHz radio | — |
| (optional, licensed variant) | NiceRF SA818S-U + UHF whip | buy only if going the ham route (`docs/04`) |
| (retired) | classic ESP32-WROOM-32, NRF24L01+ PA/LNA, MAX9814, PAM8403 | spare drawer |

## Success criteria for v1.0

- Two units on a bench exchange beacons and PTT voice reliably at 30 m.
- Five units in cars: radar shows all cars with correct relative bearing and
  distance, updating ≤ 5 s, out to at least **2 km direct / 4 km relayed**.
- PTT voice (ESP-NOW default) is intelligible at highway noise levels for a
  **tight convoy (≤ ~300 m)**; the SX1262/Codec2 transport, once field-A/B'd,
  extends that toward ~1 km. Talker's initials show on every radar.
- Voice end-to-end latency stays "walkie-talkie instant" (< 250 ms).
- A unit power-cycles with ignition and is on the radar within 60 s of
  GPS cold-start (typically much faster warm).
- No lockups or resets over a 2-hour drive, any unit.

## Document map

| Doc | Contents |
|---|---|
| `01-architecture.md` | Firmware structure, FreeRTOS tasks, dataflow, memory budget |
| `02-hardware.md` | BOM, full pin map, wiring tables, power tree, antennas |
| `03-radio-protocol.md` | LoRa link config, packet formats, duty budget, relay |
| `04-voice.md` | Digital voice: audio pipeline, codecs, jitter buffer, transport abstraction (ESP-NOW / SX1262-Codec2), legality, SA818 licensed-variant appendix |
| `05-gps-geo.md` | NMEA parsing, geodesy math, neighbour table, staleness |
| `06-ui.md` | Radar screen spec — exact layout, colours, states |
| `07-dev-guide.md` | ESP-IDF setup, build/flash/test commands, provisioning |
| `../ROADMAP.md` | Milestones and task ordering |
| `../tasks/` | Self-contained implementation tasks (the "bulk build" queue) |
