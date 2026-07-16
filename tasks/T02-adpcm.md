# T02 — `adpcm`: IMA ADPCM codec component

*(Un-superseded by the v3 digital-voice architecture — `docs/00` decision
log. Voice is digital again, and ADPCM is the ESP-NOW transport's codec.)*

**Depends:** none · **Phase:** M1 (pure C)
**Required reading:** `docs/04-voice.md` §Codecs + §voice frame

## Goal

Standard IMA/DVI ADPCM: 16-bit PCM ↔ 4-bit codes, with **externally visible
codec state** so every voice frame can carry its own decoder seed
(loss-resilient, per `docs/04`).

## Deliverables

- `firmware/components/adpcm/include/adpcm.h`
- `firmware/components/adpcm/adpcm.c`
- `firmware/components/adpcm/CMakeLists.txt` (copy convoy_proto's pattern)
- `test/host/test_adpcm.c` (Makefile already wires `SRCS_test_adpcm`)

## Interface contract

```c
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t predictor;
    uint8_t step_index;   /* 0..88 */
} adpcm_state_t;

void adpcm_init(adpcm_state_t *st);            /* predictor 0, index 0 */

/* Encode n samples (n may be odd; final nibble padded with a repeat of the
 * last code). out must hold (n+1)/2 bytes. LOW nibble = earlier sample.
 * st is updated in place — snapshot it BEFORE calling to fill the frame seed. */
void adpcm_encode(adpcm_state_t *st, const int16_t *pcm, size_t n, uint8_t *out);

/* Decode n_samples from packed nibbles into pcm. st updated in place. */
void adpcm_decode(adpcm_state_t *st, const uint8_t *in, size_t n_samples,
                  int16_t *pcm);
```

Standard IMA tables: 89-entry step table (7 … 32767) and the 16-entry index
table `{-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8}`. Clamp predictor to int16,
step_index to [0,88].

## Test requirements

1. **Round-trip SNR**: 2 s synthetic speech (sines 200/700/1900 Hz, ±12000,
   8 kHz) encode→decode; SNR > 25 dB. Silence stays < ±64.
2. **Chunked == whole**: encoding in `CL_VOICE_FRAME_SAMPLES` (160) chunks,
   state carried across calls, is byte-identical to one whole-buffer call;
   same for decode.
3. **Per-frame reseed exactness (the point)**: encode 10 frames, snapshot
   `adpcm_state_t` before each. Decode frames 0–4 and 6–9 (frame 5 "lost"),
   seeding a fresh decoder from each frame's snapshot: output for every
   decoded frame is **bit-identical** to the loss-free decode of that frame.
4. **Odd n** handles without OOB (ASan); step_index never leaves [0,88] when
   fed 10 k random bytes through decode.

## Acceptance — CI

`make -C test/host test` green.

## Out of scope

Framing/packetisation (T18 `voice_pipe`), I²S (T12), Codec2 (T22), any I/O.
