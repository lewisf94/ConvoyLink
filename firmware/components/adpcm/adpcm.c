/* adpcm.c — see include/adpcm.h and docs/04-voice.md */
#include "adpcm.h"

static const int16_t step_table[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const int8_t index_table[16] = {-1, -1, -1, -1, 2, 4, 6, 8,
                                       -1, -1, -1, -1, 2, 4, 6, 8};

void adpcm_init(adpcm_state_t *st)
{
    st->predictor = 0;
    st->step_index = 0;
}

/* Advance state by one 4-bit code, returning the reconstructed sample.
 * Shared by encoder (to track the decoder's view) and decoder. */
static int16_t step_decode(adpcm_state_t *st, uint8_t code)
{
    int32_t step = step_table[st->step_index];

    /* diffq = (step/8) + (b2)*step + (b1)*step/2 + (b0)*step/4 */
    int32_t diffq = step >> 3;
    if (code & 4) {
        diffq += step;
    }
    if (code & 2) {
        diffq += step >> 1;
    }
    if (code & 1) {
        diffq += step >> 2;
    }

    int32_t pred = st->predictor;
    if (code & 8) {
        pred -= diffq;
    } else {
        pred += diffq;
    }
    if (pred > 32767) {
        pred = 32767;
    } else if (pred < -32768) {
        pred = -32768;
    }
    st->predictor = (int16_t)pred;

    int idx = st->step_index + index_table[code];
    if (idx < 0) {
        idx = 0;
    } else if (idx > 88) {
        idx = 88;
    }
    st->step_index = (uint8_t)idx;

    return st->predictor;
}

static uint8_t encode_sample(adpcm_state_t *st, int16_t sample)
{
    int32_t step = step_table[st->step_index];
    int32_t diff = (int32_t)sample - st->predictor;

    uint8_t code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    if (diff >= step) {
        code |= 4;
        diff -= step;
    }
    if (diff >= (step >> 1)) {
        code |= 2;
        diff -= step >> 1;
    }
    if (diff >= (step >> 2)) {
        code |= 1;
    }

    step_decode(st, code); /* keep encoder state == decoder state */
    return code;
}

void adpcm_encode(adpcm_state_t *st, const int16_t *pcm, size_t n,
                  uint8_t *out)
{
    uint8_t code = 0;
    for (size_t i = 0; i < n; i++) {
        code = encode_sample(st, pcm[i]);
        if (i & 1u) {
            out[i / 2] |= (uint8_t)(code << 4);
        } else {
            out[i / 2] = code; /* low nibble = earlier sample */
        }
    }
    if (n & 1u) {
        /* pad the final high nibble with a repeat of the last code */
        out[n / 2] |= (uint8_t)(code << 4);
    }
}

void adpcm_decode(adpcm_state_t *st, const uint8_t *in, size_t n_samples,
                  int16_t *pcm)
{
    for (size_t i = 0; i < n_samples; i++) {
        uint8_t byte = in[i / 2];
        uint8_t code = (i & 1u) ? (byte >> 4) : (byte & 0x0Fu);
        pcm[i] = step_decode(st, code);
    }
}
