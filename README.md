# ConvoyLink

[![CI](https://github.com/lewisf94/ConvoyLink/actions/workflows/ci.yml/badge.svg)](https://github.com/lewisf94/ConvoyLink/actions/workflows/ci.yml)

**GPS radar + push-to-talk voice for groups road-tripping in separate cars —
no phones, no mobile signal, its own radio network.**

Each car gets one dash-mounted ESP32 unit: hold the PTT button to talk to
the whole convoy, and watch everyone else's position live on a radar-style
screen — your car centred, the others as labelled dots with distance,
bearing and freshness. Runs off the 12 V accessory socket and starts with
the ignition. Up to 5 cars.

```
 ┌──────────────────────┐
 │ U0 LF     9◦    ◂RX  │   ← you, GPS sats, someone's talking
 │      · · N · ·       │
 │    ·     ¹ᵏᵐ    ·    │
 │   ·   AM●        ·   │   ← convoy on radar,
 │  ·        ▲       ·  │      you at the centre
 │   ·     RK○42s   ·   │   ← stale dot = last seen 42 s ago
 │      · · · · ·       │
 │ AM 320m │ RK 1.1km   │   ← nearest first
 └──────────────────────┘
```

## How it works (v2 two-radio architecture)

| Subsystem | Choice |
|---|---|
| MCU | ESP32-S3-DevKitC-1 (N16R8) — ESP-IDF v5.3, no Arduino |
| Position link | **LoRa** (Semtech SX1262, EBYTE E22-900M22S) @ 869/915 MHz — 32-byte beacons every 5 s, **single-hop relay through middle cars**. Same radio class Meshtastic proves at 1–5 miles car-to-car |
| Voice | **Analog UHF FM** (NiceRF SA818S-U) — a complete walkie-talkie module; the ESP32 keys PTT and sets channel/CTCSS over UART, so firmware contains **zero audio code**. Channel plan is region config (PMR446 / ham / GMRS / UHF-CB — docs/04) |
| Display | 2.8" ILI9341 SPI, 5 Hz strip-rendered radar with range rings, compass, staleness tiers (live → stale → ghost — dots age, they don't vanish) |
| Range honesty | Radar **2–5 km** typical (more relayed / line-of-sight), voice **1–5 km** — terrain and antenna placement dominate (docs/00 §Range expectations, docs/02 §Antennas) |

## Repository tour

| Path | What |
|---|---|
| `docs/00…07` | Design docs — architecture, wiring, radio protocol, audio, geo, UI, dev guide. **Start at `docs/00-brief.md`** |
| `ROADMAP.md` | Milestones M1–M6 with gates |
| `tasks/` | The implementation queue: 21 self-contained task specs with interface contracts + acceptance tests, designed for execution by AI coding agents (`CLAUDE.md` holds the standing rules). `tasks/STATUS.md` is the live board |
| `firmware/` | ESP-IDF components + apps (`convoylink` + per-subsystem `bringup_*` test firmwares) |
| `test/host/` | gcc/ASan unit tests for the pure-C core (protocol, geo, parser, renderer) — no hardware needed |
| `sim/` | SDL2 desktop simulator: the real radar code fed by scripted GPS scenarios (arrives in T07) |

## Status

Scaffold complete on the **v2 two-radio architecture** (LoRa positions +
analog UHF voice — see the decision log in `docs/00-brief.md` for why NRF24
was retired): docs, task queue, CI, and the `convoy_proto` wire-format
component (host-tested, 9/9). Implementation proceeds through
`tasks/STATUS.md` — milestone M1 (pure-C libraries) is next. New hardware
to order per unit: an SX1262 LoRa module, an SA818S-U voice module, and
two whip antennas (~£25–35/unit — `docs/00` §Owned hardware).

## Quick start (contributors / agents)

```sh
make -C test/host test        # host unit tests — no toolchain needed beyond gcc
# firmware (needs ESP-IDF v5.3):
. ~/esp-idf/export.sh && ./tools/ci_build_apps.sh
```

Read `CLAUDE.md` before touching anything; the docs are binding and the
task files say exactly what to build and how it's accepted.

## License

Apache-2.0 — see `LICENSE`.
