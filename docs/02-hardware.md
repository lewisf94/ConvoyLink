# 02 — Hardware: BOM, Pin Map, Wiring, Power (v3)

Target board: **ESP32-S3-DevKitC-1 (N16R8)** — the 44-pin Espressif
reference board (ESP32-S3-WROOM-1, 16 MB flash, 8 MB octal PSRAM, dual
USB-C). All pin numbers below are GPIO numbers.

v3 change summary: voice is now **digital** (`docs/04`) — the SA818 UHF
module and the whole analog audio chain (MAX9814/PAM8403) are gone,
replaced by an **INMP441 I²S mic** and **MAX98357A I²S amp**. Positions
still ride the **SX1262 LoRa** radio, which now *also* carries the optional
Codec2 voice transport; the default voice transport is **ESP-NOW** on the
S3's own 2.4 GHz radio (no extra parts). If you use a different S3 board,
only this pin table changes.

## Bill of materials (per unit)

| # | Part | Qty | Role |
|---|---|---|---|
| 1 | ESP32-S3-DevKitC-1 (N16R8) | 1 | MCU |
| 2 | GY-NEO6MV2 GPS module + patch antenna | 1 | Position |
| 3 | **EBYTE E22-900M22S (SX1262, +22 dBm)** on a 2.54 mm breakout | 1 | LoRa position link |
| 4 | **INMP441 I²S MEMS microphone** | 1 | Voice in (digital) |
| 5 | **MAX98357A I²S class-D amp** | 1 | Voice out (drives 4 Ω speaker) |
| 6 | 868/915 MHz whip antenna (SMA, ~¼-wave) | 1 | LoRa (positions + Codec2 voice) |
| 7 | 2.8" ILI9341 SPI TFT 240×320 (XPT2046 touch unused) | 1 | Radar display |
| 8 | AIYIMA 40 mm 4 Ω 3 W speaker | 1 | Voice out |
| 9 | MP1584EN mini buck converter | 2 | 12 V→5.0 V and 12 V→3.3 V |
| 10 | Momentary push button (PTT) + aux button | 2 | Talk / zoom |
| 11 | Schottky diode ≥2 A + inline fuse 2 A + 12 V plug | 1 | Supply |
| 12 | Caps: 470 µF/25 V, 100 µF, 10 µF, 100 nF ×4 | — | Bulk + module decoupling |

Voice needs **no UHF radio and no analog audio parts** — the INMP441 and
MAX98357A are I²S digital, and the transport is either the S3's own 2.4 GHz
radio (ESP-NOW) or the SX1262 already on the board (Codec2). The SA818 +
UHF whip appear only in the licensed-variant appendix (`docs/04`).

Buy the SX1262 **on a carrier board with 2.54 mm headers and SMA**; wire by
signal name and follow each breakout's silkscreen. The INMP441 mic and
MAX98357A amp are I²S digital modules that wire straight to the S3 — no DAC,
no analog audio path, no mic pre-amp.

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
| 40 | `I2S_BCLK` | INMP441 SCK + MAX98357A BCLK | shared bit clock |
| 41 | `I2S_WS` | INMP441 WS + MAX98357A LRC | shared word select |
| 42 | `I2S_DIN` | INMP441 SD → ESP32 | mic data in |
| 47 | `I2S_DOUT` | ESP32 → MAX98357A DIN | speaker data out |
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

### INMP441 I²S microphone

| Module pin | Connect to |
|---|---|
| VDD | 3V3 | GND | GND |
| SCK | GPIO 40 (`I2S_BCLK`) |
| WS | GPIO 41 (`I2S_WS`) |
| SD | GPIO 42 (`I2S_DIN`) |
| L/R | **GND** (left channel — mic occupies the left slot) |

### MAX98357A I²S amplifier

| Module pin | Connect to |
|---|---|
| Vin | 5 V rail A (2.5–5.5 V ok) | GND | GND |
| BCLK | GPIO 40 (`I2S_BCLK`, shared) |
| LRC | GPIO 41 (`I2S_WS`, shared) |
| DIN | GPIO 47 (`I2S_DOUT`) |
| GAIN | leave floating = 9 dB (or tie per board table) |
| SD | leave high/floating = enabled, left-channel decode |
| ± out | 4 Ω speaker (bridged — **never ground a speaker wire**) |

Both I²S modules share `I2S_BCLK` + `I2S_WS`; the S3 runs one I²S peripheral
in full-duplex (`docs/04`). Put 100 nF across each module's supply. No
heatsink, no pre-amp, no AGC — digital in, class-D out. Idle current is
negligible; the amp draws up to ~1 W of audio into the 4 Ω speaker, budget
it on rail A.

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
                                   │                    ├─ MAX98357A amp
                                   │                    └─ GPS VCC
                                   └── Buck B → 3.3 V ──── SX1262 only (100 µF local)
S3 devkit 3V3 out ──┬─ TFT + backlight
                    └─ INMP441 mic
```

Set both bucks with a multimeter **before** connecting loads (Buck A 5.0 V,
Buck B 3.3 V). Rail A dips briefly when the amp hits loud audio peaks —
100 µF near the MAX98357A covers it. Auto-start comes free from the switched
accessory socket. Feed Buck A's 5 V into the board's **5V** pin; the onboard
LDO makes the 3V3 rail. (No UHF transmitter means no 1 A TX surge to plan
for — the v3 supply is easier than v2's.)

## Antennas (this is where range is won)

- Two antennas per unit: GPS patch (sky view) and the **868/915 LoRa whip**
  (positions, and the Codec2 voice transport). The S3's own PCB/IPEX antenna
  handles ESP-NOW voice — no third antenna.
- LoRa whip **vertical**, high in the cabin, ≥ 20 cm from the GPS patch.
- An SMA extension to a window/roof mount is the single biggest range
  upgrade for the LoRa link.
- Mount all five cars roughly the same way or range will be asymmetric.
- (Licensed SA818 variant only: add a UHF whip and never key TX without it.)
