/**
 * voice_proto — the digital voice frame wire format. This is to voice
 * what convoy_proto is to beacons, and the single source of truth for the
 * layout is docs/04-voice.md §voice frame.
 *
 * Voice frames are a **separate format from the 32-byte LoRa beacon**
 * (magic 0xC8, not 0xC7) and ride the voice transport, never the beacon
 * link — see CLAUDE.md's hard invariants.
 *
 * Pure C: no ESP-IDF headers, no allocation, host-testable.
 */
#ifndef VOICE_PROTO_H
#define VOICE_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "convoy_cfg.h"

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error "voice_proto assumes a little-endian target"
#endif

#define VF_PROTO_VER 1u

enum { VF_CODEC_ADPCM = 1, VF_CODEC_CODEC2_3200 = 2 };

#define VF_F_START 0x01u
#define VF_F_END 0x02u

/**
 * Header, then the payload immediately after, n_bytes long.
 *
 * The field list below is transcribed exactly from docs/04 §voice frame.
 * Its trailing comment there says "9 B header", but the fields sum to
 * **10** (1+1+1+1+2+1+3) — the comment is a miscount, not a different
 * layout, and the same slip makes its "ESP-NOW frame ~89 B" really 90.
 * The fields are the contract, so VF_HDR_SIZE follows them; flagged in
 * tasks/STATUS.md for a doc correction.
 */
typedef struct __attribute__((packed)) {
    uint8_t magic;     /* VF_MAGIC                                     */
    uint8_t ver_codec; /* (VF_PROTO_VER << 4) | codec                  */
    uint8_t sender;    /* uid 0..CL_MAX_UNITS-1 — talker identity      */
    uint8_t flags;     /* VF_F_START | VF_F_END                        */
    uint16_t seq;      /* per-sender, wraps; compare with cl_seq_newer */
    uint8_t n_bytes;   /* payload length                               */
    uint8_t seed[3];   /* ADPCM predictor(2) + step_index(1); 0 for Codec2 */
} vf_hdr_t;

#define VF_HDR_SIZE 10u

/* One 20 ms ADPCM frame: 160 samples at 4 bits = 80 bytes. */
#define VF_ADPCM_MAX_BYTES ((CL_VOICE_FRAME_SAMPLES + 1) / 2)
#define VF_FRAME_MAX (VF_HDR_SIZE + VF_ADPCM_MAX_BYTES)

_Static_assert(sizeof(vf_hdr_t) == VF_HDR_SIZE, "vf_hdr_t must be 10 bytes");
_Static_assert(VF_ADPCM_MAX_BYTES == 80, "20 ms ADPCM frame is 80 bytes");

/** Codec id from an already-validated frame. */
static inline uint8_t vf_codec(const vf_hdr_t *h)
{
    return (uint8_t)(h->ver_codec & 0x0Fu);
}

static inline uint8_t vf_version(const vf_hdr_t *h)
{
    return (uint8_t)((h->ver_codec >> 4) & 0x0Fu);
}

typedef enum {
    VF_OK = 0,
    VF_ERR_SIZE = -1,    /* shorter than a header, or truncated payload */
    VF_ERR_MAGIC = -2,
    VF_ERR_VERSION = -3,
    VF_ERR_CODEC = -4,
    VF_ERR_SENDER = -5,  /* uid out of range                           */
    VF_ERR_LENGTH = -6,  /* n_bytes impossible for the codec           */
} vf_err_t;

/**
 * Validate a received frame: magic, version, codec, sender range, and
 * that `len` actually covers VF_HDR_SIZE + n_bytes. Returns VF_OK or a
 * negative vf_err_t. Never trusts n_bytes before checking it against len.
 */
int vf_validate(const uint8_t *buf, size_t len);

#endif /* VOICE_PROTO_H */
