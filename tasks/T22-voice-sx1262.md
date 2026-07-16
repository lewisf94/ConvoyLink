# T22 — SX1262 / Codec2 voice transport (range-upgrade experiment)

**Depends:** T16, T19 · **Phase:** M6 / experimental (not on the v1.0
critical path)
**Required reading:** `docs/04-voice.md` §Transport B + §Codecs; `docs/03`
§Channel access rules (the shared-radio arbitration)

## Goal

A second `voice_transport` that carries voice over the **SX1262 in GFSK
mode** using **Codec2 3200** at 869.7–870 MHz / ≤ 5 mW ERP — the
licence-free sub-GHz range upgrade. Same `voice_pipe`, same `voice_task`;
only the transport and codec change. Then A/B it against ESP-NOW in the car.

## Deliverables

- `firmware/components/codec2/` — vendored Codec2 (3200 mode subset) with a
  thin `c2_encode(160 samples → 8 B)` / `c2_decode(8 B → 160 samples)` API
  and a `NOTICE` recording origin + licence. Host-testable round-trip test
  (`test/host/test_codec2.c`, SNR/intelligibility sanity).
- `voice_pipe`: add `VF_CODEC_CODEC2_3200` support (the frame already has a
  codec field; wire the alternate encode/decode path — no jitter changes).
- `firmware/components/voice_transport/vt_sx1262.c` — implements
  `voice_transport_t` but **does not touch the chip directly**: it submits
  voice frames to `radio_task` and receives them from it via queues.
- `firmware/apps/convoylink/main/radio_task.c` — extend with GFSK voice
  arbitration (`docs/03` §4): on a voice burst, switch SX1262 LoRa→GFSK,
  give voice priority, suspend beacon RX for the burst, then switch back and
  resume. Config from `convoy_cfg.h` (`CL_VOICE_GFSK_FREQ_HZ`, `_BR_BPS`),
  TX power set so **ERP ≤ 5 mW** (verify with the range app).
- `bringup_radio`: add a `gfsk`/`voiceping` mode to measure the 5 mW link's
  RSSI/loss vs distance before trusting it.

## Acceptance — CI

Host tests (incl. Codec2 round-trip) + `./tools/ci_build_apps.sh` green.

## Acceptance — hardware (owner)

- [ ] `unitcfg voice sx1262` on two units → PTT conversation works, Codec2
      quality noted (lower than ADPCM, still intelligible)
- [ ] Beacons keep flowing between/around voice bursts (arbitration OK — the
      radar must not freeze while talking)
- [ ] Confirm TX power gives ERP ≤ 5 mW (the whole legal basis)
- [ ] **A/B vs ESP-NOW** on the same drive: record the range each gives in
      `tasks/STATUS.md`; the winner becomes the documented default in
      `docs/00`/`docs/04`, the loser stays a config option

## Out of scope

Making it the default before the field A/B (that's a doc decision after the
data). Analog SA818 (licensed-variant appendix only).
