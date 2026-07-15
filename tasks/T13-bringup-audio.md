# T13 — `bringup_voice` app: channel / PTT / RSSI exerciser

*(v2: replaces the original `bringup_audio` ADC/DAC bench — voice moved to
the analog SA818 module, `docs/00` decision log. Filename kept for queue
stability.)*

**Depends:** T12 · **Phase:** M3
**Required reading:** `docs/04-voice-sa818.md`; `docs/02` §SA818 wiring

## Goal

Prove the SA818 end to end on real hardware: channel programming, PTT
keying, and that a human voice actually gets from one unit's mic to
another unit's speaker.

## Deliverables

- `firmware/apps/bringup_voice/…` (copy the app template)

## Behaviour (console commands)

| Command | Action |
|---|---|
| `chan <plan> <n>` | `sa818_set_channel()` from the `docs/04` table (e.g. `chan PMR446 3`) |
| `ptt on\|off` | key/unkey manually — talk while `on`, listen while `off` |
| `rssi` | one-shot `sa818_rssi()` print |
| `mon` | 5×/s print of RSSI + carrier-present while idle, until Enter |
| `vol <1-8>` | `sa818_set_volume()` |
| `holdtx <s>` | key PTT for exactly `s` seconds then release (burn-in / thermal check) |

## Acceptance — CI

`./tools/ci_build_apps.sh` green.

## Acceptance — hardware (owner checklist, needs 2 units)

- [ ] `chan PMR446 3` on both units, `unitcfg show`-equivalent confirms
      same frequency/CTCSS
- [ ] Unit A `ptt on`, speak, `ptt off`: unit B's speaker plays it back
      intelligibly with no ESP32 code in the audio path
- [ ] `mon` on B shows `carrier_present` flip true while A transmits
- [ ] Swap roles, confirm both directions
- [ ] `holdtx 60` — module stays warm, not hot; if it's too hot to touch,
      add/upsize the heatsink before longer sessions
- [ ] A cheap PMR446 handheld set to the same channel+CTCSS hears the
      unit and is heard by it (interop sanity, docs/04)
- [ ] Range walk: note distance where speech becomes unreadable through
      static — record in `tasks/STATUS.md` next to the LoRa figure from T10

## Out of scope

LoRa/GPS/display, PTT-button-to-state-machine wiring (T18), any
DSP/codec work (there is none).
