# T13 — `bringup_audio` app: tone / meter / echo / switch bench

**Depends:** T12 · **Phase:** M3
**Required reading:** `docs/04-audio.md`; `docs/02` §Audio wiring

## Goal

Prove the whole analog chain (MAX9814 → ADC, DAC → PAM8403 → speaker) and
measure the half-duplex mode-switch — the number that decides whether the
voice design flies.

## Deliverables

- `firmware/apps/bringup_audio/…` (copy the app template)

## Behaviour (console commands)

| Command | Action |
|---|---|
| `tone [hz] [s]` | PLAYBACK mode; synthesize sine (default 440 Hz, 2 s, 50 % FS) through `aio_write` |
| `meter` | CAPTURE mode; 10×/s print RMS + peak as a bar: `mic ▏█████░░░░░▕ rms=1234 pk=8100`; Enter stops |
| `echo [s]` | record N s (default 3) of mic to RAM (16 KB/s), switch modes, play it back — the end-to-end proof |
| `switch [n]` | bench: alternate CAPTURE↔PLAYBACK n times (default 100), report min/avg/**max** switch µs; FAIL line if max ≥ 100 ms |
| `vol <0-100>` | playback volume |

## Acceptance — CI

`./tools/ci_build_apps.sh` green.

## Acceptance — hardware (owner checklist)

- [ ] `tone` — clean tone from speaker, no gross distortion at vol 70
- [ ] `tone 3000` audible, `tone 200` audible (speaker band sanity)
- [ ] `meter` — quiet room rms < 300; speaking at arm's length swings
      peak > 5000 without pegging at 32767 (if pegged: MAX9814 gain
      jumper down / move mic)
- [ ] `echo` — recorded speech plays back intelligibly (this is the
      voice-quality preview: it's 12-bit/8 kHz — walkie-talkie, not hi-fi)
- [ ] `switch 100` — max switch time printed and **< 100 ms** ← the gate.
      Record the number in `tasks/STATUS.md`. If it fails, T12 falls back
      to the esp_timer/dac_oneshot path (docs/04) before M5 proceeds
- [ ] Engine-noise dry run optional: run `meter` in a car at speed, note
      rms floor (informs squelch/gain tuning later)

## Out of scope

Radio, ADPCM, PTT button handling (T18).
