# 02 — Hardware: BOM, Pin Map, Wiring, Power

Target board: **classic ESP32-WROOM-32 dev board, 30-pin, USB-C** ("DevKit v1"
pinout). All pin numbers below are GPIO numbers, not header positions.

## Bill of materials (per unit)

| # | Part | Qty | Role |
|---|---|---|---|
| 1 | ESP32 DevKit v1 30-pin USB-C | 1 | MCU |
| 2 | GY-NEO6MV2 GPS module + patch antenna | 1 | Position |
| 3 | NRF24L01+ PA/LNA + SMA antenna | 1 | Radio |
| 4 | 2.8" ILI9341 SPI TFT 240×320 (XPT2046 touch) | 1 | Radar display |
| 5 | MAX9814 mic amp module | 1 | Voice in |
| 6 | PAM8403 audio amp board | 1 | Voice out |
| 7 | AIYIMA 40 mm 4 Ω 3 W speaker | 1 | Voice out |
| 8 | MP1584EN mini buck converter | 2 | 12 V→5.0 V and 12 V→3.3 V |
| 9 | Momentary push button (PTT) | 1 (+1 optional aux) | Talk / zoom |
| 10 | Schottky diode ≥2 A (e.g. SS34/1N5822) | 1 | Reverse-polarity |
| 11 | Caps: 470 µF/25 V, 100 µF, 10 µF, 100 nF ×3 | — | Bulk + radio decoupling |
| 12 | Inline fuse 2 A + 12 V accessory plug | 1 | Supply |

## Pin map (single source of truth — do not re-derive in code reviews)

Firmware must take these from `convoy_pins.h` (created in task T15); this
table is the authority for that header.

| GPIO | Direction | Net | Peripheral | Notes |
|---|---|---|---|---|
| 18 | out | `TFT_SCK` | ILI9341 + XPT2046 (VSPI) | IOMUX pin → 40 MHz capable |
| 23 | out | `TFT_MOSI` | ILI9341 + XPT2046 | |
| 19 | in | `TFT_MISO` | XPT2046 (ILI9341 MISO optional) | |
| 5 | out | `TFT_CS` | ILI9341 | Strapping-safe (idles high) |
| 2 | out | `TFT_DC` | ILI9341 | Strapping pin; output-only after boot — OK. Onboard LED shares it |
| 4 | out | `TFT_RST` | ILI9341 | |
| 32 | out | `TFT_BL` | Backlight | LEDC PWM, brightness control |
| 33 | out | `TOUCH_CS` | XPT2046 | Stretch feature; idle high |
| 39 (VN) | in | `TOUCH_IRQ` | XPT2046 | Input-only, **needs external 10 k pull-up** if used |
| 14 | out | `RF_SCK` | NRF24 (HSPI via GPIO matrix) | ≤8 MHz — matrix routing fine |
| 13 | out | `RF_MOSI` | NRF24 | |
| 35 | in | `RF_MISO` | NRF24 | Input-only pin — ideal for MISO |
| 15 | out | `RF_CSN` | NRF24 | Strapping (MTDO): idles high = harmless |
| 27 | out | `RF_CE` | NRF24 | |
| 26 | in | `RF_IRQ` | NRF24 | Active-low, internal pull-up |
| 16 | in | `GPS_RX` | NEO-6M TX → ESP32 | UART2 |
| 17 | out | `GPS_TX` | NEO-6M RX ← ESP32 | Only needed for UBX config (optional) |
| 25 | out | `AUDIO_OUT` | DAC1 → PAM8403 L-IN | 8-bit DAC |
| 34 | in | `MIC_IN` | MAX9814 OUT → ADC1_CH6 | Input-only |
| 22 | in | `BTN_PTT` | PTT button → GND | Internal pull-up, active low |
| 21 | in | `BTN_AUX` | Aux button → GND | Internal pull-up; zoom/volume |
| 36 (VP) | in | `SENSE_12V` | 12 V via 100 k / 10 k divider | Optional; reserved |
| 12 | — | — | **Leave unconnected** | MTDI strap: pulling high at boot bricks flash voltage |
| 0 | — | — | Boot button | Keep free for flashing |
| 1, 3 | — | — | UART0 | USB console/flash |

### Strapping summary

GPIO 0, 2, 5, 12, 15 affect boot. This map only drives them from peripherals
that are inert at reset (chip-select lines idle high; TFT DC floats). The one
hard rule: **nothing may pull GPIO12 high at power-on** — hence it is unused.
If a unit boot-loops with peripherals attached and runs bare, suspect
strapping first (see `docs/07`, §Troubleshooting).

## Wiring tables

### NRF24L01+ PA/LNA (8-pin header, top view, notch left)

| NRF pin | Net | Connect to |
|---|---|---|
| 1 GND | GND | Common ground |
| 2 VCC | **3.3 V rail B** (dedicated buck) | **Never 5 V.** 100 µF + 10 µF + 100 nF directly across pins 1–2 |
| 3 CE | `RF_CE` | GPIO 27 |
| 4 CSN | `RF_CSN` | GPIO 15 |
| 5 SCK | `RF_SCK` | GPIO 14 |
| 6 MOSI | `RF_MOSI` | GPIO 13 |
| 7 MISO | `RF_MISO` | GPIO 35 |
| 8 IRQ | `RF_IRQ` | GPIO 26 |

PA/LNA modules brown out on dirty supplies and then "work" erratically —
the dedicated buck plus the capacitor stack at the module is not optional.

### 2.8" ILI9341 + XPT2046

| Module pin | Connect to | | Module pin | Connect to |
|---|---|---|---|---|
| VCC | 3V3 (devkit) | | T_CLK | GPIO 18 (shared SCK) |
| GND | GND | | T_CS | GPIO 33 |
| CS | GPIO 5 | | T_DIN | GPIO 23 (shared MOSI) |
| RESET | GPIO 4 | | T_DO | GPIO 19 (shared MISO) |
| DC | GPIO 2 | | T_IRQ | GPIO 39 + 10 k→3V3 |
| SDI/MOSI | GPIO 23 | | SDO/MISO | GPIO 19 (optional) |
| SCK | GPIO 18 | | LED | GPIO 32 via 47 Ω (or 3V3 for always-on) |

### GY-NEO6MV2 GPS

| GPS pin | Connect to |
|---|---|
| VCC | 5 V rail (module has its own LDO; I/O is 3.3 V — safe) |
| GND | GND |
| TX | GPIO 16 |
| RX | GPIO 17 |

Antenna must face the sky — mount the patch antenna on the dash top.

### Audio

```
MAX9814: VDD→3V3, GND→GND, OUT→GPIO34, GAIN→GND (50 dB) to start, A/R→float
PAM8403: 5V→5 V rail, GND→GND, L-IN→GPIO25 via 10 µF series cap, R-IN→GND via 10 µF (unused)
         L+ / L− → speaker (Class-D bridge output: NEVER ground either speaker wire)
```

If playback hisses, add an RC low-pass between DAC and amp (1 kΩ series +
10 nF to GND, fc ≈ 16 kHz). Start without it.

### Buttons

PTT and AUX: one leg to GPIO (22 / 21), other leg to GND. Internal pull-ups.
Mount PTT somewhere reachable without looking — steering-column stalk area.

## Power tree

```
12 V accessory socket ── 2 A fuse ── Schottky ──┬── 470 µF + 100 nF
                                                ├── Buck A → 5.0 V ──┬─ ESP32 VIN
                                                │                    ├─ PAM8403
                                                │                    └─ GPS VCC
                                                └── Buck B → 3.3 V ──── NRF24 only
ESP32 devkit 3V3 out ──┬─ TFT VCC/backlight
                       └─ MAX9814
```

- **Set both bucks with a multimeter before connecting anything** (pot turns
  many times; mark set position). Buck A: 5.0 V. Buck B: 3.3 V.
- Auto-start comes free: the accessory socket is switched by ignition.
  `SENSE_12V` (GPIO 36) is reserved for a future "graceful shutdown" feature —
  wire the divider if convenient, otherwise skip.
- USB-C stays free for flashing/logs; do not power from USB and 12 V decide-
  simultaneously unless the devkit has the usual VIN diode (most do — check).

## Antenna placement (this is where range is won)

- NRF24 antenna **vertical**, as high as possible, at a side window or on the
  dash near the windscreen. Metal roof + engine block shadow the rear — an
  SMA pigtail to a window mount is the single biggest range upgrade.
- Keep the NRF24 antenna ≥ 20 cm from the GPS patch antenna.
- All five cars should mount roughly the same way, or range will be
  asymmetric ("A hears B, B doesn't hear A").
