# T12 — `audio_io`: half-duplex capture/playback component

**Depends:** none · **Phase:** M3 — **the project's #1 hardware risk;
read `docs/04-audio.md` §ESP-IDF driver constraints before anything else.**
**Required reading:** `docs/04-audio.md` (all); `docs/02` §Audio wiring

## Goal

Own the ESP32's built-in ADC (mic in) and DAC (speaker out) through the
IDF v5 continuous-mode drivers, switching between them on demand
(half-duplex), presenting clean 8 kHz mono int16 PCM both ways.

## Deliverables

- `firmware/components/audio_io/include/audio_io.h`
- `firmware/components/audio_io/audio_io.c`
- `firmware/components/audio_io/CMakeLists.txt`
  (REQUIRES esp_adc, esp_driver_dac or driver, convoy_pins)

## Interface contract

```c
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

typedef enum { AIO_OFF, AIO_CAPTURE, AIO_PLAYBACK } aio_mode_t;

esp_err_t aio_init(void);                 /* allocates all buffers once   */
/* Tears down the current driver, brings up the requested one. Logs and
 * returns the measured switch time; must be < 100 ms (docs/04). */
esp_err_t aio_set_mode(aio_mode_t m, uint32_t *switch_us_out);
aio_mode_t aio_mode(void);

/* CAPTURE: blocking read of up to max conditioned samples (DC-removed,
 * gain-scaled, soft-clipped int16 @ 8 kHz). Returns count, 0 on timeout,
 * -1 if not in CAPTURE. */
int aio_read(int16_t *pcm, size_t max, uint32_t wait_ms);

/* PLAYBACK: blocking write of 8 kHz int16 samples (internally ZOH x4 to
 * 32 kHz, converted to 8-bit unsigned for the DAC, volume-scaled by
 * aio_set_volume). Returns samples accepted. */
int aio_write(const int16_t *pcm, size_t n, uint32_t wait_ms);
void aio_set_volume(uint8_t pct);         /* 0..100, default from cfg     */
```

Implementation constraints (binding, from docs/04):

- Capture: `adc_continuous` on ADC1 ch 6 (GPIO 34), 12-bit, 12 dB atten,
  8 kHz, conversion frames a multiple of 44 samples. Conditioning chain
  exactly: DC tracker `dc += (x - dc) >> 9`, scale `<<4`, optional ×2 gain
  (compile-time), soft-clip.
- Playback: `dac_continuous` on DAC ch 1 (GPIO 25) at `CL_DAC_RATE_HZ`
  (32 kHz) — the DAC DMA driver cannot run at 8 kHz directly; every input
  sample is written 4×. Idle: emit midscale then stop — no busy loop.
- The two drivers cannot coexist (both need I2S0's DMA on classic ESP32) —
  `aio_set_mode` must fully deinit one before init'ing the other, and must
  be idempotent and safe to call from one task only (document owner:
  `audio_task`).
- No allocation after `aio_init`. Measure switch time with `esp_timer`.

## Acceptance — CI

`./tools/ci_build_apps.sh` green (compiles standalone; add to T13's app).

## Acceptance — hardware

Via T13's checklist (tone, meter, echo, switch-time bench).

## Out of scope

ADPCM (T02), packetisation/jitter (T18/T19), volume UI.
