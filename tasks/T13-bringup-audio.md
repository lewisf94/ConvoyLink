# T13 — `bringup_audio` app: I²S mic / speaker / codec bench

*(v3: retargeted from the SA818 exerciser to the I²S audio bench — `docs/00`
decision log. Filename kept for queue stability.)*

**Depends:** T12 (and T02 for the codec command) · **Phase:** M3
**Required reading:** `docs/04-voice.md`; `docs/02` §INMP441 + §MAX98357A

## Goal

Prove the digital audio chain end to end on real hardware: mic captures,
speaker plays, and an ADPCM round-trip through RAM sounds intelligible.

## Deliverables

- `firmware/apps/bringup_audio/…` (copy the app template)

## Behaviour (console commands)

| Command | Action |
|---|---|
| `tone [hz] [s]` | PLAYBACK; synth sine (default 440 Hz, 2 s) through `aio_write` |
| `meter` | CAPTURE; 10×/s print RMS + peak bar: `mic ▏█████░░░░░▕ rms=1234 pk=8100`; Enter stops |
| `loop [s]` | record N s of mic to RAM, play it straight back (raw I²S sanity) |
| `codec [s]` | record → `adpcm_encode`→`adpcm_decode` → play back (voice-quality preview at 8 kHz/ADPCM) |
| `vol <0-100>` | playback volume |

## Acceptance — CI

`./tools/ci_build_apps.sh` green.

## Acceptance — hardware (owner checklist)

- [ ] `tone` — clean tone from the speaker; `tone 3000` and `tone 200`
      both audible (speaker band sanity)
- [ ] `meter` — quiet room rms < 300; talking at arm's length pushes peak
      > 5000 without pegging at 32767 (if pegged, mic too hot / too close)
- [ ] `loop` — your voice plays back clearly (raw path OK)
- [ ] `codec` — still intelligible after ADPCM (this is the voice quality
      you'll hear on-air — 8 kHz, a bit crunchy, walkie-talkie grade)
- [ ] No I²S underruns/overruns logged over a 2-minute `meter` run

## Out of scope

Radio/transport (T19), PTT-button wiring (T19), GPS/display, Codec2 (T22).
