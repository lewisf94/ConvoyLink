# 04 — Audio Pipeline (PTT Voice)

Mono speech, 8 kHz, half-duplex. The hardware chain:

```
TX:  MAX9814 → GPIO34 (ADC1_CH6) → adc_continuous DMA → DC-removal/gain
     → IMA ADPCM encode → cl_voice_t frames → radio

RX:  radio → jitter buffer → IMA ADPCM decode → ×4 sample repeat
     → dac_continuous DMA (GPIO25) → PAM8403 → speaker
```

## ESP-IDF driver constraints (read before touching `audio_io`)

This project uses **ESP-IDF v5.x**, where the old I2S built-in ADC/DAC modes
are gone. The replacements:

- **Capture**: `esp_adc/adc_continuous.h` — DMA ADC. On classic ESP32 the
  digital ADC controller is clocked through I2S0. Configure: ADC1 channel 6
  (GPIO 34), 12-bit, 12 dB attenuation, `sample_freq_hz = 8000` (valid: the
  ESP32 continuous driver accepts ~611 Hz – 83 kHz). Conversion frame size:
  a multiple of 44 samples (e.g. 176) so each DMA callback yields whole
  radio frames.
- **Playback**: `driver/dac_continuous.h` — DMA DAC. Its minimum conversion
  rate is well above 8 kHz, so **run the DAC at 32 kHz and write every
  decoded sample 4 times** (zero-order hold ×4). The ZOH images sit at
  ≥8 kHz where the 40 mm speaker barely responds; add the optional RC
  low-pass (`docs/02`) only if hiss is objectionable.
- **The conflict**: on classic ESP32 both drivers ultimately need I2S0's DMA,
  so capture and playback **cannot run simultaneously**. PTT makes the
  system half-duplex anyway. `audio_io` owns this: it is a mode machine
  (`AIO_OFF / AIO_CAPTURE / AIO_PLAYBACK`) that tears down one driver and
  brings up the other on demand. **Mode-switch time must be measured in the
  bring-up app and stay under 100 ms** — this is the project's #1 hardware
  risk, validate it before building anything on top (T12/T13).

Fallback if `dac_continuous` misbehaves: an `esp_timer` at 8 kHz calling
`dac_oneshot_output_voltage`. Accept it only if the DMA route fails.

## Signal conditioning (TX)

MAX9814 output is biased at ~1.25 V, up to ~2 Vpp. Per 12-bit sample:

1. DC removal, one-pole tracker: `dc += (x - dc) >> 9; y = x - dc;`
2. Scale to int16: `s = y << 4`, then soft-clip to ±32767.
3. Optional fixed digital gain ×1/×2 (config constant), applied before clip.

Start with the MAX9814 GAIN pin at 50 dB (tied to GND) and its AGC doing the
heavy lifting.

## Codec: IMA ADPCM (`components/adpcm`, pure C)

Standard IMA/DVI ADPCM, 16-bit PCM ↔ 4-bit codes, 89-entry step table.
State = `{int16_t predictor; uint8_t step_index;}`.

Framing rule (from `docs/03`): the encoder snapshots its state **into the
packet header before encoding each 44-sample frame**; the decoder **re-seeds
from every packet**. Decoder state carried between packets is only a
fallback for concealment. Loss of any packet is therefore self-healing.

## PTT state machine (`audio_task` + `ctrl_task`)

```
IDLE ──PTT down, channel free──────────────► TX_ACTIVE
IDLE ──PTT down, channel busy──► ARMED_WAIT ─channel free─► TX_ACTIVE
ARMED_WAIT ──PTT up──► IDLE                    (UI shows BUSY while armed)
TX_ACTIVE ──PTT up──► TX_DRAIN ──last frame(END flag) sent──► IDLE
TX_ACTIVE ──60 s cap──► TX_DRAIN               (stuck-button guard)
```

- "Channel busy" = voice frame received within the last 300 ms (`docs/03` §4).
- First frame of a burst carries `BURST_START`, last carries `BURST_END`
  (possibly with `n_samples < 44`).
- Entering TX_ACTIVE: switch `audio_io` to CAPTURE. Leaving TX_DRAIN: back to
  PLAYBACK-ready idle (AIO_OFF; playback starts on demand).
- While transmitting we do not decode incoming voice (half-duplex; radio_task
  drops RX voice frames during own burst).

## RX path & jitter buffer (`audio_task`)

- **Talker lock**: the first voice frame of a burst locks the session to that
  `sender`; frames from other senders are dropped until the burst ends
  (END flag or 300 ms without frames). UI shows the talker's initials.
- **Jitter buffer**: ring of 16 frame slots ordered by `seq`
  (`cl_seq_newer`). Prefill to 3 frames (~16.5 ms) before starting the DAC,
  then consume one frame per 5.5 ms of playback.
- **Concealment**: if the next `seq` hasn't arrived when due, replay the
  previous frame's PCM at half amplitude, up to 3 consecutive times, then
  output silence until frames resume. Frames arriving older than the play
  cursor are dropped.
- **Burst end**: END flag or 300 ms starvation → drain, stop DAC, idle.
  Keep the busy-lockout hangover (300 ms) so replies don't collide.

End-to-end latency budget: 5.5 (frame fill) + ~2 (encode+queue) + 1.5 (air)
+ ~17 (jitter prefill) + 5.5 (playback) ≈ **~32 ms** typical — comfortably
inside the 250 ms requirement, so do not add buffering "just in case".

## Levels & volume

- Playback gain: single software multiplier before DAC write. v1 ships a
  compile-time constant (`convoy_cfg.h`); AUX-button volume cycling is a
  stretch task. Hardware trim: PAM8403 boards with a pot — set ~70 %.
- Idle: write DAC midscale (128) once, then stop the driver; if amp hiss
  annoys, the RC filter is the fix (do not busy-loop the DAC).

## Numbers that live in `convoy_cfg.h` (shared with simulator/tests)

| Constant | Value |
|---|---|
| `CL_AUDIO_RATE_HZ` | 8000 |
| `CL_FRAME_SAMPLES` | 44 |
| `CL_DAC_RATE_HZ` | 32000 (×4 ZOH) |
| `CL_JITTER_SLOTS` / prefill | 16 / 3 |
| `CL_BUSY_HANGOVER_MS` | 300 |
| `CL_TX_MAX_MS` | 60000 |
