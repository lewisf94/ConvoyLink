/**
 * convoy_proto — ConvoyLink Protocol (CLP) wire formats + helpers.
 *
 * The single source of truth for these layouts is docs/03-radio-protocol.md.
 * Every packet is exactly CL_PKT_SIZE (32) bytes, little-endian, packed;
 * on-air bytes are the in-memory bytes of these structs (LE targets only,
 * enforced below).
 *
 * Pure C component: no ESP-IDF headers, no allocation, host-testable.
 * Style notes for other components (this file is the exemplar):
 *   - public names carry the component prefix (cl_ here)
 *   - int error codes: 0 = OK, negative = cl_err_t
 *   - validate external input before trusting any field
 */
#ifndef CONVOY_PROTO_H
#define CONVOY_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "convoy_cfg.h"

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error "convoy_proto assumes a little-endian target"
#endif

#define CL_MAGIC 0xC7u
#define CL_PROTO_VER 1u

typedef enum {
    CL_TYPE_BEACON = 1,
    /* 2 is reserved: was digital voice before the v2 architecture moved
     * voice to the analog SA818 link (docs/00 decision log). Never reuse. */
    CL_TYPE_PING = 3,
} cl_type_t;

typedef enum {
    CL_OK = 0,
    CL_ERR_SIZE = -1,    /* buffer not CL_PKT_SIZE               */
    CL_ERR_MAGIC = -2,   /* first byte != CL_MAGIC               */
    CL_ERR_VERSION = -3, /* protocol version mismatch            */
    CL_ERR_TYPE = -4,    /* unknown cl_type                      */
    CL_ERR_FIELD = -5,   /* a field is out of its documented range */
} cl_err_t;

/* ---- Common header (4 bytes) ------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint8_t magic;    /* CL_MAGIC                                  */
    uint8_t ver_type; /* (CL_PROTO_VER << 4) | cl_type_t           */
    uint8_t sender;   /* unit id 0..CL_MAX_UNITS-1                 */
    uint8_t meta;     /* beacon: hop (0|1); ping: unused           */
} cl_hdr_t;

/* ---- CL_TYPE_BEACON ---------------------------------------------------- */
typedef struct __attribute__((packed)) {
    cl_hdr_t hdr;
    uint16_t seq;
    char initials[2];     /* ASCII, not NUL-terminated             */
    int32_t lat_e7;       /* degrees * 1e7                         */
    int32_t lon_e7;       /* degrees * 1e7                         */
    uint16_t speed_dm_s;  /* 0.1 m/s                               */
    uint16_t course_cdeg; /* 0.01 deg, 0..35999; CL_COURSE_INVALID */
    uint8_t fix_quality;  /* 0 none, 2 2D, 3 3D                    */
    uint8_t sats;
    uint16_t fix_age_ms; /* age of fix when sent, saturating       */
    uint8_t reserved[8]; /* zero on TX, ignored on RX              */
} cl_beacon_t;

#define CL_COURSE_INVALID 0xFFFFu
#define CL_BEACON_HOP_MAX 1u

/* ---- CL_TYPE_PING (range testing) --------------------------------------- */
typedef struct __attribute__((packed)) {
    cl_hdr_t hdr;
    uint16_t seq;
    uint32_t uptime_ms;
    uint8_t pattern[22]; /* 0xA5 fill                              */
} cl_ping_t;

_Static_assert(sizeof(cl_hdr_t) == 4, "cl_hdr_t must be 4 bytes");
_Static_assert(sizeof(cl_beacon_t) == CL_PKT_SIZE, "beacon must be 32 bytes");
_Static_assert(sizeof(cl_ping_t) == CL_PKT_SIZE, "ping must be 32 bytes");

/* ---- API ---------------------------------------------------------------- */

/**
 * Validate a received 32-byte payload.
 *
 * Checks magic, version, type, sender range and type-specific field ranges
 * (voice: step_index/n_samples; beacon: hop/course). Returns the cl_type_t
 * (> 0) on success or a cl_err_t (< 0). `len` guards against short reads.
 */
int cl_validate(const uint8_t *buf, size_t len);

/** Type of an already-validated buffer. */
static inline cl_type_t cl_type(const uint8_t *buf)
{
    return (cl_type_t)(((const cl_hdr_t *)buf)->ver_type & 0x0Fu);
}

/**
 * seq wrap-safe comparison: true iff a is newer than b (docs/03).
 * Use this everywhere; never compare seq with < or >.
 */
static inline bool cl_seq_newer(uint16_t a, uint16_t b)
{
    return (int16_t)(uint16_t)(a - b) > 0;
}

/** Fill a beacon in place. Zeroes reserved bytes, clamps hop. */
void cl_make_beacon(cl_beacon_t *out, uint8_t sender, uint16_t seq,
                    const char initials[2], int32_t lat_e7, int32_t lon_e7,
                    uint16_t speed_dm_s, uint16_t course_cdeg,
                    uint8_t fix_quality, uint8_t sats, uint32_t fix_age_ms);

/** Rewrite a received hop-0 beacon as its hop-1 relay copy (docs/03). */
void cl_beacon_to_relay(cl_beacon_t *b);

/** Fill a ping packet (bring-up range testing). */
void cl_make_ping(cl_ping_t *out, uint8_t sender, uint16_t seq,
                  uint32_t uptime_ms);

#endif /* CONVOY_PROTO_H */
