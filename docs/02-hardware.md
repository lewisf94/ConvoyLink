# 02 — Hardware: BOM, Pin Map, Wiring, Power (v2)

Target board: **classic ESP32-WROOM-32 dev board, 30-pin, USB-C** ("DevKit v1"
pinout). All pin numbers are GPIO numbers, not header positions.

v2 change summary: NRF24 out; **SX1262 LoRa** (position link) takes over its
SPI pins; **SA818S-U** (analog UHF voice) takes over the audio path — the
ESP32 no longer touches audio samples at all.

## Bill of materials (per unit)

| # | Part | Qty | Role |
|---|---|---|---|
| 1 | ESP32 DevKit v1 30-pin USB-C | 1 | MCU |
| 2 | GY-NEO6MV2 GPS module + patch antenna | 1 | Position |
| 3 | **EBYTE E22-900M22S (SX1262, +22 dBm)** on a breakout/test board with 2.54 mm headers (or Waveshare Core1262) | 1 | LoRa position link |
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
| 15 | Resistors: 47 kΩ, 10 kΩ ×2, small heatsink for SA818 | — | Audio pad, pull-ups, thermal |

Buy modules **on carrier/adapter boards with 2.54 mm headers and SMA** —
the bare E22-M22S and SA818 are stamp-hole SMD parts. Wire by **signal
name** below and follow the breakout's silkscreen.

## Pin map (single source of truth — `convoy_pins.h` is generated from this)

| GPIO | Dir | Net | Peripheral | Notes |
|---|---|---|---|---|
| 18 | out | `TFT_SCK` | ILI9341 (VSPI) | IOMUX pin → 40 MHz |
| 23 | out | `TFT_MOSI` | ILI9341 | |
| 19 | in | `TFT_MISO` | ILI9341 (optional) | |
| 5 | out | `TFT_CS` | ILI9341 | Strapping-safe (idles high) |
| 2 | out | `TFT_DC` | ILI9341 | Strapping pin; driven only after boot |
| 4 | out | `TFT_RST` | ILI9341 | |
| 32 | out | `TFT_BL` | Backlight | LEDC PWM |
| 14 | out | `LORA_SCK` | SX1262 (HSPI via matrix) | ≤10 MHz |
| 13 | out | `LORA_MOSI` | SX1262 | |
| 35 | in | `LORA_MISO` | SX1262 | Input-only pin — ideal |
| 15 | out | `LORA_NSS` | SX1262 | Strapping (MTDO): idles high, harmless |
| 27 | out | `LORA_NRST` | SX1262 | |
| 26 | in | `LORA_DIO1` | SX1262 IRQ | RX-done/TX-done |
| 34 | in | `LORA_BUSY` | SX1262 | Input-only pin — ideal |
| 25 | out | `LORA_TXEN` | E22 RF switch | |
| 33 | out | `LORA_RXEN` | E22 RF switch | |
| 16 | in | `GPS_RX` | NEO-6M TX → ESP32 | UART2 |
| 17 | out | `GPS_TX` | NEO-6M RX ← ESP32 | UBX config only (optional) |
| 21 | out | `VHF_TXD` | ESP32 → SA818 RXD | UART1 (remapped) 9600 |
| 36 (VP) | in | `VHF_RXD` | SA818 TXD → ESP32 | Input-only pin — ideal |
| 22 | out | `VHF_PTT` | SA818 PTT | **Low = transmit**; idle high |
| 39 (VN) | in | `BTN_PTT` | PTT button → GND | Input-only: **external 10 k pull-up to 3V3 required** |
| 0 | in | `BTN_AUX` | Devkit BOOT button (reuse) | Zoom/backlight. Don't hold while powering on (enters bootloader) |
| 12 | — | — | **Leave unconnected** | MTDI strap — pulling high at boot bricks flash voltage |
| 1, 3 | — | — | UART0 | USB console/flash |

Not GPIO-wired: SA818 `PD` → **tie to 3.3 V** (always on); SA818 `H/L` →
**solder jumper**: open = 1 W, to GND = 0.5 W (set per legal channel plan,
`docs/04`). v1's `SENSE_12V` and XPT2046 touch are descoped — no pins left;
noted as the price of the richer radio set.

### Strapping summary

GPIO 0, 2, 5, 12, 15 affect boot. This map drives them only from things
inert at reset (CS lines idle high; TFT DC floats; BOOT button unpressed).
Hard rules: nothing on GPIO 12; don't hold AUX during power-on. A unit that
boot-loops wired but runs bare → suspect strapping (`docs/07`).

## Wiring by module

### SX1262 (E22-900M22S breakout)

| Module signal | Connect to | | Module signal | Connect to |
|---|---|---|---|---|
| VCC | **3.3 V rail B** | | NSS | GPIO 15 |
| GND | GND | | SCK | GPIO 14 |
| DIO1 | GPIO 26 | | MOSI | GPIO 13 |
| BUSY | GPIO 34 | | MISO | GPIO 35 |
| NRST | GPIO 27 | | TXEN | GPIO 25 |
| ANT | 868/915 whip (SMA) | | RXEN | GPIO 33 |

100 µF + 100 nF directly across VCC/GND at the module. **3.3 V only — 5 V
kills it.** TX draws ~120 mA peaks at +22 dBm.

### SA818S-U (carrier board)

| Signal | Connect to |
|---|---|
| VCC | **5 V rail A**, via 220 µF + 100 nF + 10 nF at the module |
| GND | GND (star point at buck A) |
| RXD / TXD | GPIO 21 / GPIO 36 |
| PTT | GPIO 22 (low = TX) |
| PD | 3.3 V (tied) |
| H/L | solder jumper (open = 1 W / GND = 0.5 W) |
| MIC_IN | MAX9814 `OUT` → **10 µF (+ toward MAX9814) → 47 kΩ → MIC_IN** |
| AF_OUT | **10 µF → PAM8403 L-IN** (add 10 k pot before it if volume trim wanted) |
| ANT | UHF whip (SMA) — **never key TX without the antenna fitted** |

Notes (from field-proven builds): the module runs warm on TX — stick a
small heatsink on the ground pad near the antenna end; budget **1 A** on
the 5 V rail during TX; poor supply decoupling shows up as hum/noise on
your transmitted audio; MAX9814 GAIN → GND (50 dB) to start and let the
SA818's limiter + the 47 k pad do the rest (if others report distortion,
move GAIN to 40 dB). PAM8403 outputs are bridged — never ground a speaker
wire.

### GY-NEO6MV2 GPS

VCC → 5 V rail A · GND → GND · TX → GPIO 16 · RX → GPIO 17.
Patch antenna sky-up on the dash.

### ILI9341 display

VCC → devkit 3V3 · GND → GND · CS 5 · RESET 4 · DC 2 · MOSI 23 · SCK 18 ·
MISO 19 (optional) · LED → GPIO 32 via 47 Ω. Touch pins unconnected (v2).

### Buttons

PTT: GPIO 39 → button → GND, plus **10 kΩ from GPIO 39 to 3.3 V** (input-
only pins have no internal pull-up). AUX: the devkit BOOT button is already
wired to GPIO 0 — extend a parallel button to the case if you want it
reachable.

## Power tree

```
12 V acc ── 2 A fuse ── Schottky ──┬── 470 µF + 100 nF
                                   ├── Buck A → 5.0 V ──┬─ ESP32 VIN
                                   │                    ├─ SA818 (220 µF local)
                                   │                    ├─ PAM8403
                                   │                    └─ GPS VCC
                                   └── Buck B → 3.3 V ──── SX1262 only (100 µF local)
ESP32 devkit 3V3 ──┬─ TFT + backlight
                   └─ MAX9814
```

Set both bucks with a multimeter **before** connecting loads. Rail A must
hold 5.0 V while the SA818 transmits (scope/multimeter during a long PTT —
sag below ~4.5 V = raise wire gauge / check the buck). Auto-start comes
free from the switched accessory socket.

## Antennas (this is where range is won)

- Three antennas per unit: GPS patch (sky view), 868/915 LoRa whip, UHF
  voice whip. Both whips **vertical**, high in the cabin (windscreen top
  corners / side windows), ideally ≥ 30 cm apart and ≥ 20 cm from the GPS
  patch.
- An SMA extension to a window/roof mount is the single biggest range
  upgrade for both radios; glass-mount or mag-mount UHF antennas are cheap.
- Mount all five cars roughly the same way or range will be asymmetric.
- **Never transmit on the SA818 without its antenna** — reflected power
  damages the PA.
