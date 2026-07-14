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
convoy> unitcfg show
unit_id=2 initials=JD
```

Valid ids: 0–4, unique per convoy. Initials: exactly 2 ASCII chars A–Z/0–9.
Un-provisioned units boot as `U? --`, transmit nothing, and show a
`PROVISION ME` banner.

## Host unit tests (no hardware)

```sh
make -C test/host          # builds + runs every test binary; non-zero exit on failure
```

Pure-C components (`convoy_proto`, `convoy_geo`, `adpcm`, `nmea`,
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
| Boot loop with peripherals attached, fine bare | Strapping pin pulled — check nothing sits on GPIO 12, and GPIO 2/15 wiring matches `docs/02` |
| `flash read err` / brownout resets | Buck A undervolt or USB+12 V fight; measure 5 V under load |
| NRF24 registers read `0x00`/`0xFF` | 3.3 V rail B missing/dirty, missing caps, or MISO not on GPIO 35 — run `bringup_radio` register dump |
| Radio "works on the bench, garbage in the car" | Supply noise — caps at the module, shorter leads, dedicated buck |
| No GPS fix | Needs sky. Cold start can take minutes; check `bringup_gps` shows sentences (module OK) vs silence (wiring) |
| Display stays white | RESET/DC swapped, or CS not on GPIO 5; run `bringup_display` |
| Mic buzzes at idle | Ground loop / leads too long; star-ground at buck A output, twist mic pair |
| Clicks on PTT press/release | Expected small artefact from ADC↔DAC mode switch; must be < 100 ms (T12 measures it) |
| `idf.py` not found | Shell missing `. ~/esp-idf/export.sh` |

## Serial console conventions

115200 baud. Log tags = component names (`ESP_LOGI(TAG, ...)`).
Default level INFO; `radio` and `audio` drop to WARN in release builds
(they log per-packet at DEBUG only — never at INFO, it starves the console).
