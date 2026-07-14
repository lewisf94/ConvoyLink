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
 │ U0 LF     9◦    ◂AM  │   ← you, GPS sats, who's talking
 │      · · N · ·       │
 │    ·     ¹ᵏᵐ    ·    │
 │   ·   AM●        ·   │   ← convoy on radar,
 │  ·        ▲       ·  │      you at the centre
 │   ·     RK○42s   ·   │   ← stale dot = last seen 42 s ago
 │      · · · · ·       │
 │ AM 320m │ RK 1.1km   │   ← nearest first
 └──────────────────────┘
```

## How it works

| Subsystem | Choice |
|---|---|
| MCU | ESP32-WROOM-32 (classic, 30-pin devkit) — ESP-IDF v5.3, no Arduino |
| Radio | NRF24L01+ PA/LNA, 2.476 GHz, 250 kbps, fixed 32-byte broadcast frames — one link carries **both** voice and GPS |
| Voice | PTT, 8 kHz mono IMA ADPCM, per-packet codec state (loses 5.5 ms per lost packet, nothing more), ~30 ms end-to-end |
| Positions | NEO-6M GPS, beacon every 5 s, **single-hop relay through middle cars** stretches radar coverage past direct radio range |
| Display | 2.8" ILI9341 SPI, 5 Hz strip-rendered radar with range rings, compass, staleness tiers (live → stale → ghost — dots age, they don't vanish) |
| Range honesty | Realistically **300 m – 1 km** car-to-car (docs/00 §Range reality). Out-of-range cars show as ghosts with "last seen"; the protocol is transport-agnostic for future SA818/LoRa experiments |

## Repository tour

| Path | What |
|---|---|
| `docs/00…07` | Design docs — architecture, wiring, radio protocol, audio, geo, UI, dev guide. **Start at `docs/00-brief.md`** |
| `ROADMAP.md` | Milestones M1–M6 with gates |
| `tasks/` | The implementation queue: 21 self-contained task specs with interface contracts + acceptance tests, designed for execution by AI coding agents (`CLAUDE.md` holds the standing rules). `tasks/STATUS.md` is the live board |
| `firmware/` | ESP-IDF components + apps (`convoylink` + per-subsystem `bringup_*` test firmwares) |
| `test/host/` | gcc/ASan unit tests for the pure-C core (protocol, geo, codec, parser, renderer) — no hardware needed |
| `sim/` | SDL2 desktop simulator: the real radar code fed by scripted GPS scenarios (arrives in T07) |

## Status

Scaffold complete: docs, task queue, CI, and the `convoy_proto` wire-format
component (host-tested, 9/9). Implementation proceeds through
`tasks/STATUS.md` — milestone M1 (pure-C libraries) is next.

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
