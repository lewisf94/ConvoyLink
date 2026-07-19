/**
 * adpcm — standard IMA/DVI ADPCM: 16-bit PCM <-> 4-bit codes, with the
 * codec state exposed so every voice frame can carry its own decoder seed
 * (per-frame loss resilience, docs/04-voice.md §Codecs).
 *
 * Pure C component: no ESP-IDF headers, no allocation, host-testable.
 */
#ifndef ADPCM_H
#define ADPCM_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t predictor;
    uint8_t step_index; /* 0..88 */
} adpcm_state_t;

/** Reset to the initial state (predictor 0, step_index 0). */
void adpcm_init(adpcm_state_t *st);

/**
 * Encode n samples into packed 4-bit codes; out must hold (n+1)/2 bytes.
 * LOW nibble = earlier sample. Odd n pads the final high nibble with a
 * repeat of the last code (filler only — state advances once per sample).
 * st is updated in place — snapshot it BEFORE calling to seed a frame.
 */
void adpcm_encode(adpcm_state_t *st, const int16_t *pcm, size_t n,
                  uint8_t *out);

/** Decode n_samples from packed nibbles into pcm. st updated in place. */
void adpcm_decode(adpcm_state_t *st, const uint8_t *in, size_t n_samples,
                  int16_t *pcm);

#endif /* ADPCM_H */
