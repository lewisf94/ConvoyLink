# T12 — `audio_io`: I²S mic capture + speaker playback

*(v3: retargeted from the SA818 UART driver to the S3 I²S audio path —
`docs/00` decision log. Voice is digital again; filename kept for queue
stability.)*

**Depends:** none · **Phase:** M3 (ESP-IDF)
**Required reading:** `docs/04-voice.md` §Capture/playback; `docs/02`
§INMP441 + §MAX98357A wiring

## Goal

Own the S3's I²S audio: INMP441 mic in and MAX98357A amp out, presenting
clean 8 kHz mono int16 PCM both ways. One I²S peripheral in full-duplex
(shared BCLK/WS), half-duplex use (PTT).

## Deliverables

- `firmware/components/audio_io/include/audio_io.h`
- `firmware/components/audio_io/audio_io.c`
- `firmware/components/audio_io/CMakeLists.txt` (REQUIRES `esp_driver_i2s`, convoy_pins)

## Interface contract

```c
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

typedef enum { AIO_OFF, AIO_CAPTURE, AIO_PLAYBACK } aio_mode_t;

esp_err_t aio_init(void);        /* one I²S full-duplex channel on CONVOY_PIN_I2S_*,
                                    8 kHz mono; allocates all DMA buffers once  */
esp_err_t aio_set_mode(aio_mode_t m);   /* enable RX, TX, or neither            */
aio_mode_t aio_mode(void);

/* CAPTURE: blocking read of up to max int16 samples @ 8 kHz. INMP441 is
 * 24-bit in a 32-bit slot (left) — take the top 16 bits. Returns count,
 * 0 on timeout, -1 if not in CAPTURE. */
int aio_read(int16_t *pcm, size_t max, uint32_t wait_ms);

/* PLAYBACK: blocking write of 8 kHz int16 mono to the MAX98357A (16-bit).
 * Volume-scaled by aio_set_volume. Returns samples accepted. */
int aio_write(const int16_t *pcm, size_t n, uint32_t wait_ms);
void aio_set_volume(uint8_t pct);       /* 0..100 */
```

Implementation notes (binding, from `docs/04`): new `driver/i2s_std.h` API
(not the legacy `i2s.h`); one `i2s_chan` pair (TX+RX) sharing BCLK/WS on
`CONVOY_PIN_I2S_BCLK`/`_WS`, `I2S_DIN` = RX data, `I2S_DOUT` = TX data;
sample rate `CL_AUDIO_RATE_HZ`; DMA frame a small multiple of
`CL_VOICE_FRAME_SAMPLES`. Mono: mic in left slot, amp left-channel. No
allocation after `aio_init`. `aio_read` conditions to int16 (top bits +
optional fixed gain, soft-clip). Idle (`AIO_OFF`): stop TX so the amp mutes.

## Acceptance — CI

`./tools/ci_build_apps.sh` green (compiles standalone; exercised by T13).

## Acceptance — hardware

Via T13's checklist (tone, mic meter, loopback).

## Out of scope

Codec (T02), framing/jitter (T18), transport (T19), PTT logic (T19).
