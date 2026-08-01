/**
 * audio_io — the S3's I²S audio path: INMP441 mic in, MAX98357A amp out,
 * presenting plain 8 kHz mono int16 PCM in both directions
 * (docs/04-voice.md §Capture/playback, docs/02 §INMP441 + §MAX98357A).
 *
 * One I²S peripheral runs full-duplex so BCLK/WS are shared between the
 * two modules exactly as wired; PTT means only one direction is ever
 * *used* at a time, which `aio_set_mode` selects.
 *
 * Slot width is 32 bits in both directions. The INMP441 needs it (24-bit
 * data left-justified in a 32-bit slot), and a shared bit clock cannot
 * serve two different slot widths, so playback samples ride in the top 16
 * bits of a 32-bit slot — the MAX98357A latches the MSBs and is happy
 * with 32-bit frames. The API stays int16 PCM either way.
 *
 * No allocation happens after `aio_init`. Only `voice_task` may call
 * these (CLAUDE.md); they are not internally serialised.
 */
#ifndef AUDIO_IO_H
#define AUDIO_IO_H

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

typedef enum { AIO_OFF, AIO_CAPTURE, AIO_PLAYBACK } aio_mode_t;

/**
 * Brings up one full-duplex I²S channel pair on the CONVOY_PIN_I2S_* pins
 * at CL_AUDIO_RATE_HZ mono, allocating every DMA buffer once. Leaves the
 * mode AIO_OFF (both directions stopped, so the amp is silent).
 * Idempotent: a second call returns ESP_OK without reallocating.
 */
esp_err_t aio_init(void);

/**
 * Enable RX, TX, or neither. Switching directions stops the other one
 * first, so the amp mutes whenever we are not playing back.
 */
esp_err_t aio_set_mode(aio_mode_t m);

aio_mode_t aio_mode(void);

/**
 * CAPTURE: blocking read of up to `max` int16 samples at 8 kHz. Returns
 * the sample count, 0 on timeout, or -1 if not in AIO_CAPTURE.
 */
int aio_read(int16_t *pcm, size_t max, uint32_t wait_ms);

/**
 * PLAYBACK: blocking write of 8 kHz int16 mono. Scaled by
 * `aio_set_volume`. Returns samples accepted, 0 on timeout, or -1 if not
 * in AIO_PLAYBACK.
 */
int aio_write(const int16_t *pcm, size_t n, uint32_t wait_ms);

/** Playback volume, 0..100 (clamped). Applied in `aio_write`. */
void aio_set_volume(uint8_t pct);

#endif /* AUDIO_IO_H */
