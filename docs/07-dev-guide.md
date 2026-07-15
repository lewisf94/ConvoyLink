# 07 — Developer Guide

## Toolchain

- **ESP-IDF v5.3.x** (pin: latest v5.3 patch release). Install:
  ```sh
  git clone -b v5.3.2 --depth 1 --recursive https://github.com/espressif/esp-idf.git ~/esp-idf
  ~/esp-idf/install.sh esp32
  . ~/esp-idf/export.sh          # per shell; add an alias
  ```
- **Host tests**: any gcc + make (used by CI and the pure-C components).
- **Simulator**: `libsdl2-dev` + gcc + make.

## Build / flash / monitor

Each directory under `firmware/apps/` is a standalone ESP-IDF project that
pulls shared code via `EXTRA_COMPONENT_DIRS = ../../components`.

```sh
cd firmware/apps/convoylink        # or any bringup_* app
idf.py set-target esp32            # once per app checkout
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor   # Ctrl-] exits monitor
```

All five units run the **same binary** — identity lives in NVS (below).

## Provisioning a unit (once per device)

With `idf.py monitor` attached to the `convoylink` app (console implemented
in T15):

```
convoy> unitcfg set 2 JD        # unit_id 2, initials "JD"
convoy> unitcfg region EU       # EU | US | AU — selects the LoRa frequency (docs/03)
convoy> unitcfg voice PMR446 3  # channel plan + channel number (docs/04 table)
convoy> unitcfg show
unit_id=2 initials=JD region=EU voice=PMR446 ch3 (446.05625 MHz, CTCSS 88.5)
```

Valid ids: 0–4, unique per convoy. Initials: exactly 2 ASCII chars A–Z/0–9.
All five units must share the same region and voice channel. Remember the
SA818 H/L power jumper must match the plan (docs/02, docs/04).
Un-provisioned units boot as `U? --`, transmit nothing, and show a
`PROVISION ME` banner.

## Host unit tests (no hardware)

```sh
make -C test/host          # builds + runs every test binary; non-zero exit on failure
```

Pure-C components (`convoy_proto`, `convoy_geo`, `nmea`,
`neighbor_table`, `radar_render`) get their tests here. Adding a component
test: drop `test_<name>.c` into `test/host/` — the Makefile picks up
`test_*.c` automatically and links sources listed in its `SRCS_<name>`
variable (see the convoy_proto example already present).

## Simulator

```sh
make -C sim
./sim/build/convoysim sim/scenarios/overtake.csv --pov 0        # windowed
./sim/build/convoysim sim/scenarios/overtake.csv --pov 0 --dump out/   # headless PNGs
```

Keys: `0–4` switch point-of-view unit, `z` cycle zoom, `space` pause,
`←/→` scrub. Scenario CSV format (header row required):
`t_ms,unit_id,initials,lat,lon,speed_mps,course_deg` — see `sim/scenarios/`.

## CI (`.github/workflows/ci.yml`)

| Job | What it does |
|---|---|
| `host-tests` | `make -C test/host` on ubuntu-latest gcc |
| `firmware` | ESP-IDF v5.3 container; `tools/ci_build_apps.sh` builds **every** `firmware/apps/*` project |
| `sim` | builds the simulator (SDL2), runs `--dump` on one scenario as a smoke test (once sim exists) |

A task is not done until CI is green (`tasks/README.md`).

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| Boot loop with peripherals attached, fine bare | Strapping pin pulled — check nothing sits on GPIO 12, AUX/BOOT not held, and GPIO 2/15 wiring matches `docs/02` |
| `flash read err` / brownout resets | Buck A undervolt or USB+12 V fight; measure 5 V under load (SA818 TX pulls ~1 A) |
| SX1262 init fails / BUSY stuck high | 3.3 V rail B missing/dirty, NRST not on GPIO 27, or TXEN/RXEN swapped — run `bringup_radio` |
| LoRa "works on the bench, deaf in the car" | Antenna flat instead of vertical, or supply noise — caps at the module, window mount |
| SA818 ignores AT commands | TX/RX crossed (ESP32 TX→module RXD), PD not tied high, or 9600 baud mismatch — run `bringup_voice` |
| Your voice transmits with hum | SA818 supply decoupling missing (220 µF at module), or mic leads running beside the power wiring |
| Nobody hears you / you hear nobody | Channel/CTCSS mismatch between units (`unitcfg show` on both), H/L jumper, or squelch too high |
| No GPS fix | Needs sky. Cold start can take minutes; check `bringup_gps` shows sentences (module OK) vs silence (wiring) |
| Display stays white | RESET/DC swapped, or CS not on GPIO 5; run `bringup_display` |
| `idf.py` not found | Shell missing `. ~/esp-idf/export.sh` |

## Serial console conventions

115200 baud. Log tags = component names (`ESP_LOGI(TAG, ...)`).
Default level INFO; `radio` and `voice` drop to WARN in release builds
(per-packet / per-poll logging at DEBUG only — never at INFO, it starves
the console).
