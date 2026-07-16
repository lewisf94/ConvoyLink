# 02 — Hardware: BOM, Pin Map, Wiring, Power (v2.1)

Target board: **ESP32-S3-DevKitC-1 (N16R8)** — the 44-pin Espressif
reference board (ESP32-S3-WROOM-1, 16 MB flash, 8 MB octal PSRAM, dual
USB-C). All pin numbers below are GPIO numbers.

v2.1 change summary: MCU is the **ESP32-S3** (classic WROOM-32 retired —
voice went analog so the on-chip DAC that pinned us to the classic part is
no longer needed). Radios unchanged from v2: **SX1262 LoRa** for positions,
**SA818S-U** analog UHF for voice. If you use a different S3 board, only
this pin table changes.

## Bill of materials (per unit)

| # | Part | Qty | Role |
|---|---|---|---|
| 1 | ESP32-S3-DevKitC-1 (N16R8) | 1 | MCU |
| 2 | GY-NEO6MV2 GPS module + patch antenna | 1 | Position |
| 3 | **EBYTE E22-900M22S (SX1262, +22 dBm)** on a 2.54 mm breakout | 1 | LoRa position link |
| 4 | **NiceRF SA818S-U (400–480 MHz, 1 W)** on a carrier/dev board with SMA | 1 | Voice transceiver |
| 5 | 868/915 MHz whip antenna (SMA, ~¼-wave) | 1 | LoRa |
| 6 | UHF 430–470 MHz whip antenna (SMA) | 1 | Voice |
| 7 | 2.8" ILI9341 SPI TFT 240×320 (XPT2046 touch unused) | 1 | Radar display |
| 8 | MAX9814 mic amp module | 1 | Voice mic (feeds SA818) |
| 9 | PAM8403 audio amp board | 1 | Voice out (fed by SA818) |
| 10 | AIYIMA 40 mm 4 Ω 3 W speaker | 1 | Voice out |
| 11 | MP1584EN mini buck converter | 2 | 12 V→5.0 V and 12 V→3.3 V |
| 12 | Momentary push button (PTT) | 1 | Talk |
| 13 | Schottky diode ≥2 A + inline fuse 2 A + 12 V plug | 1 | Supply |
| 14 | Caps: 470 µF/25 V, 220 µF, 100 µF, 10 µF ×2, 100 nF ×4, 10 nF | — | Bulk + module decoupling |
| 15 | Resistors: 47 kΩ, 10 kΩ, small heatsink for SA818 | — | Audio pad, thermal |

Buy the SX1262 and SA818 **on carrier boards with 2.54 mm headers and SMA**;
wire by signal name and follow each breakout's silkscreen. The mic and
speaker wire to the **SA818**, never to the ESP32 — the S3 has no DAC and
firmware never touches audio.

## Pin map (single source of truth — `convoy_pins.h` is generated from this)

ESP32-S3-DevKitC-1 (N16R8). Every GPIO here is a general-purpose,
bidirectional pin with an internal pull-up available (the classic ESP32's
"input-only" pins are gone — buttons need no external pull-up).

| GPIO | Net | Peripheral | Notes |
|---|---|---|---|
| 12 | `TFT_SCK` | ILI9341 (SPI2/FSPI) | 40 MHz |
| 11 | `TFT_MOSI` | ILI9341 | |
| 13 | `TFT_MISO` | ILI9341 (optional) | |
| 10 | `TFT_CS` | ILI9341 | |
| 14 | `TFT_DC` | ILI9341 | |
| 9 | `TFT_RST` | ILI9341 | |
| 21 | `TFT_BL` | Backlight | LEDC PWM |
| 5 | `LORA_SCK` | SX1262 (SPI3) | ≤10 MHz |
| 6 | `LORA_MOSI` | SX1262 | |
| 4 | `LORA_MISO` | SX1262 | |
| 7 | `LORA_NSS` | SX1262 | |
| 15 | `LORA_RST` | SX1262 | |
| 16 | `LORA_BUSY` | SX1262 | |
| 17 | `LORA_DIO1` | SX1262 IRQ | RX-done / TX-done |
| 18 | `LORA_TXEN` | E22 RF switch | |
| 8 | `LORA_RXEN` | E22 RF switch | |
| 38 | `GPS_RX` | NEO-6M TX → ESP32 | UART1 |
| 39 | `GPS_TX` | NEO-6M RX ← ESP32 | UBX config only (optional) |
| 40 | `VHF_TXD` | ESP32 → SA818 RXD | UART2, 9600 |
| 41 | `VHF_RXD` | SA818 TXD → ESP32 | UART2 |
| 42 | `VHF_PTT` | SA818 PTT | **Low = transmit**; idle high |
| 1 | `BTN_PTT` | PTT button → GND | Internal pull-up, active low |
| 2 | `BTN_AUX` | Aux button → GND | Internal pull-up; zoom/backlight |
| 48 | `STATUS_LED` | Onboard WS2812 RGB | Optional boot/fault indicator |

### Reserved — never assign these

| GPIO | Why |
|---|---|
| 26–32 | In-package **SPI flash** (all S3 modules) |
| 33–37 | In-package **octal PSRAM** (N16R8) — the reason those pins are gone |
| 19, 20 | **Native USB** (D-/D+) — keep for the USB-JTAG/serial console |
| 43, 44 | **UART0** (TXD0/RXD0) — the other console route |
| 0 | BOOT strapping button — keep free for flashing |
| 3, 45, 46 | Strapping pins — leave unconnected |

The S3 has two USB-C ports: one via the onboard USB-UART bridge, one native
USB. Either flashes and gives a console; don't power from both a USB port
and the 12 V supply at once.

### Strapping summary

Boot-affecting pins are 0, 3, 45, 46. None carry a peripheral in this map
(all radio CS/reset lines sit on ordinary GPIOs), so there is no strapping
hazard by construction — a big simplification over the classic board. If a
unit boot-loops wired but runs bare, suspect a solder bridge onto 0/3/45/46
first (`docs/07`).

## Wiring by module

### SX1262 (E22-900M22S breakout)

| Module signal | Connect to | | Module signal | Connect to |
|---|---|---|---|---|
| VCC | **3.3 V rail B** | | NSS | GPIO 7 |
| GND | GND | | SCK | GPIO 5 |
| DIO1 | GPIO 17 | | MOSI | GPIO 6 |
| BUSY | GPIO 16 | | MISO | GPIO 4 |
| NRST | GPIO 15 | | TXEN | GPIO 18 |
| ANT | 868/915 whip (SMA) | | RXEN | GPIO 8 |

100 µF + 100 nF directly across VCC/GND at the module. **3.3 V only — 5 V
kills it.** TX draws ~120 mA peaks at +22 dBm.

### SA818S-U (carrier board)

| Signal | Connect to |
|---|---|
| VCC | **5 V rail A**, via 220 µF + 100 nF + 10 nF at the module |
| GND | GND (star point at buck A) |
| RXD / TXD | GPIO 40 / GPIO 41 |
| PTT | GPIO 42 (low = TX) |
| PD | 3.3 V (tied) |
| H/L | solder jumper — **default GND = 0.5 W** for the PMR446 plan (docs/04) |
| MIC_IN | MAX9814 `OUT` → **10 µF (+ toward MAX9814) → 47 kΩ → MIC_IN** |
| AF_OUT | **10 µF → PAM8403 L-IN** (optional 10 k trim pot before it) |
| ANT | UHF whip (SMA) — **never key TX without the antenna fitted** |

Field-proven notes: heatsink the ground pad near the antenna end (runs warm
on TX); budget **1 A** on the 5 V rail during TX; weak VCC decoupling shows
up as hum on your transmitted audio; MAX9814 GAIN → GND (50 dB) to start.
PAM8403 outputs are bridged — never ground a speaker wire.

### GY-NEO6MV2 GPS

VCC → 5 V rail A · GND → GND · TX → GPIO 38 · RX → GPIO 39.
Patch antenna sky-up on the dash.

### ILI9341 display

VCC → devkit 3V3 · GND → GND · CS 10 · RESET 9 · DC 14 · MOSI 11 · SCK 12 ·
MISO 13 (optional) · LED → GPIO 21 via 47 Ω. Touch pins unconnected.

### Buttons

PTT: GPIO 1 → button → GND. AUX: GPIO 2 → button → GND. Both use internal
pull-ups (`gpio_pullup_en`) — no external resistors, and no input-only-pin
restriction on the S3.

## Power tree

```
12 V acc ── 2 A fuse ── Schottky ──┬── 470 µF + 100 nF
                                   ├── Buck A → 5.0 V ──┬─ S3 devkit "5V" pin
                                   │                    ├─ SA818 (220 µF local)
                                   │                    ├─ PAM8403
                                   │                    └─ GPS VCC
                                   └── Buck B → 3.3 V ──── SX1262 only (100 µF local)
S3 devkit 3V3 out ──┬─ TFT + backlight
                    └─ MAX9814
```

Set both bucks with a multimeter **before** connecting loads (Buck A 5.0 V,
Buck B 3.3 V). Rail A must hold 5.0 V while the SA818 transmits — check it
under a long PTT; sag below ~4.5 V means thicker wire or a better buck.
Auto-start comes free from the switched accessory socket. Feed Buck A's
5 V into the board's **5V** pin; the onboard LDO makes the 3V3 rail.

## Antennas (this is where range is won)

- Three antennas per unit: GPS patch (sky view), 868/915 LoRa whip, UHF
  voice whip. Both whips **vertical**, high in the cabin, ≥ 30 cm apart and
  ≥ 20 cm from the GPS patch.
- An SMA extension to a window/roof mount is the single biggest range
  upgrade for both radios; glass-mount or mag-mount UHF antennas are cheap.
- Mount all five cars roughly the same way or range will be asymmetric.
- **Never transmit on the SA818 without its antenna** — reflected power
  damages the PA.
