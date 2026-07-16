# T19 — ESP-NOW voice: transport + `voice_task` + UI — closes M5

*(v3: un-superseded; was "voice RX". Now the integration that makes digital
PTT voice work over the default ESP-NOW transport. Filename kept.)*

**Depends:** T12, T13, T18, T17 · **Phase:** M5
**Required reading:** `docs/04-voice.md` §Transport abstraction + §Transport A
+ §PTT state machine + §invariant; `docs/06` status-bar states

## Goal

Hold PTT, talk, and everyone in ESP-NOW range hears you — with the talker's
initials on every radar. Wire `audio_io` + `voice_pipe` + the ESP-NOW
transport together in `voice_task`, and show the voice states in the UI.

## Deliverables

- `firmware/components/voice_transport/include/voice_transport.h` — the
  `voice_transport_t` interface (`docs/04`) + a selector
  `voice_transport_get(const char *name)`.
- `firmware/components/voice_transport/vt_espnow.c` — the ESP-NOW transport:
  `esp_wifi_init`+STA+start on `CL_ESPNOW_CHANNEL`, `esp_now_init`, broadcast
  peer; `send`=`esp_now_send`, `recv` drains an RX-callback queue, `busy` =
  frame seen within `CL_VOICE_HANGOVER_MS`. **Never associate/host/scan**
  (the softened invariant).
- `firmware/components/voice_transport/CMakeLists.txt` (REQUIRES esp_wifi, esp_now)
- `firmware/apps/convoylink/main/voice_task.c` — real implementation:
  - boot: `aio_init`, `voice_transport_get(nvs)` → init; failure → `VOICE?`
    state + retry every 5 s (radar unaffected)
  - PTT events from `ctrl_q` → the `docs/04` state machine (IDLE / ARMED_WAIT
    on busy / TX / TX_DRAIN; 60 s cap). TX: `aio_set_mode(CAPTURE)` →
    `aio_read` → `vp_tx_feed` → `transport.send`
  - RX: `transport.recv` → `vp_rx_offer`; when a burst is active
    `aio_set_mode(PLAYBACK)` and pump `vp_rx_pull` → `aio_write`; end → OFF
  - publish `voice_status ∈ {IDLE, TX, RX(uid), BUSY, FAULT}` to shared state
- `ui_task.c`: render states per `docs/06` — `TX` tile, `◂<initials>` for the
  talker, `BUSY` while ARMED_WAIT, `VOICE?` on FAULT; talker's chip border
- `unit_cfg`: add `voice espnow|sx1262` (T15 stub extended) + a `voice`
  console diagnostic (transport, state, tx-seconds, frames in/out/dropped)

## Acceptance — CI

`./tools/ci_build_apps.sh` + host tests + sim smoke green.

## Acceptance — hardware (owner checklist == M5 gate, 2 units)

- [ ] Two units, same `voice` transport, 5 m apart: PTT conversation both
      ways, intelligible, no lockups over 20 exchanges
- [ ] Receiver's status bar shows the talker's initials; clears < 1 s after
      release
- [ ] Both press at once → later presser sees BUSY, gets the channel when
      the first releases (ARMED_WAIT)
- [ ] Latency clap test subjectively "instant" (< ~¼ s) — note it in STATUS
- [ ] Radar keeps updating during a 30 s monologue (independent radios)
- [ ] Range walk: distance where ESP-NOW voice breaks up — record in
      `tasks/STATUS.md` (calibrates the ~150–400 m estimate; motivates T22)

## Out of scope

The SX1262/Codec2 transport (T22), roger beeps, VOX, per-unit volume memory.
