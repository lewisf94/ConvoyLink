# T18 — Voice integration: PTT, carrier detect, UI — closes M5

*(v2: rewritten from the ADPCM TX framer to SA818 integration — `docs/00`
decision log. Filename kept for queue stability. T19 was absorbed here.)*

**Depends:** T13, T17 · **Phase:** M5
**Required reading:** `docs/04-voice-sa818.md` §PTT state machine +
§Carrier detect (binding); `docs/06` status-bar states

## Goal

Hold PTT, talk; see who's on air. Wire the SA818 into the real firmware:
PTT state machine driven by the button, carrier-detect polling for the RX/
BUSY indicators, channel config from NVS at boot. The audio itself never
enters firmware — this task is pure control logic.

## Deliverables (within `firmware/apps/convoylink/main/`)

- `voice_task.c` — real implementation:
  - boot: apply `unit_cfg`'s voice channel via `sa818_set_channel`
    (+ volume); on failure mark `VOICE?` state, retry every 5 s
  - consume PTT events from `ctrl_q`; run the docs/04 state machine
    exactly (IDLE / ARMED_WAIT / TX, 60 s stuck-guard, 500 ms busy
    hangover)
  - while idle, poll `sa818_rssi` every `CL_VOICE_RSSI_POLL_MS`;
    carrier → `voice_status = RX`; recently cleared → `BUSY`
  - publish `voice_status ∈ {IDLE, TX, RX, BUSY, FAULT}` to shared state
- `ui_task.c` — render the states per docs/06: `TX` tile, generic `◂RX`,
  `BUSY` banner while ARMED_WAIT, `VOICE?` tile on FAULT
- `unitcfg voice …` already stores the plan (T15); add a `voice` console
  command for live diagnostics: current channel, RSSI, state, TX seconds
  today

## Acceptance — CI

`./tools/ci_build_apps.sh` + host tests + sim smoke green (no renderer
changes expected beyond the states T06 already supports).

## Acceptance — hardware (owner checklist == M5 gate, 2 units)

- [ ] Bench, two units 5 m apart on the same plan: PTT conversation both
      directions, intelligible, zero firmware involvement in the audio
- [ ] Receiver's status bar shows `◂RX` while the other talks; clears
      within ~1 s of release (poll + hangover)
- [ ] Press PTT while the other unit is transmitting: BUSY shows, TX
      starts automatically the moment they release (ARMED_WAIT works)
- [ ] Hold PTT 61 s: TX cuts at 60 s (stuck-button guard), logs say why
- [ ] Radar dots keep updating during a 30 s monologue (planes are
      independent — this should be boringly true)
- [ ] Field: intelligible at ≥ 1 km between cars (record actual distance
      and terrain in STATUS.md next to the T10/T13 walk-test numbers)

## Out of scope

Roger beep, VOX, CTCSS scanning, talker identification (analog FM has
none — docs/04), any DSP.
