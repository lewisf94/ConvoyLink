/**
 * nmea — byte-feed NMEA 0183 parser for RMC + GGA sentences.
 *
 * Pure C component: no ESP-IDF headers, no allocation, host-testable.
 * All fields are integer/fixed-point (docs/05-gps-geo.md) — no floating
 * point is used anywhere in this parser.
 *
 * Usage:
 *   nmea_parser_t p; nmea_init(&p);
 *   nmea_event_t ev = nmea_feed(&p, byte);
 *   const nmea_fix_t *fix = nmea_fix(&p);
 */
#ifndef NMEA_H
#define NMEA_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;          /* RMC status == 'A' (last RMC seen)          */
    int32_t lat_e7;      /* degrees * 1e7, signed (S negative)         */
    int32_t lon_e7;      /* degrees * 1e7, signed (W negative)         */
    uint16_t speed_dm_s; /* 0.1 m/s, from knots                       */
    uint16_t course_cdeg; /* 0.01 deg, 0..35999; 0xFFFF when absent    */
    uint8_t fix_quality;  /* 0 none, else 3 (GGA fix indicator >= 1)   */
    uint8_t sats;         /* GGA satellites used                       */
    uint16_t hdop_x10;    /* GGA HDOP * 10                             */
    uint32_t utc_hms;     /* hhmmss as decimal (logging only)          */
} nmea_fix_t;

typedef enum {
    NMEA_EV_NONE = 0,
    NMEA_EV_RMC,
    NMEA_EV_GGA,
    NMEA_EV_BAD,
} nmea_event_t;

/* Opaque to callers; sized to stay well under 128 bytes, no pointers. */
typedef struct {
    uint8_t state;
    char body[80];
    uint8_t len;
    uint8_t total;
    uint8_t cksum;
    uint8_t cksum_hi;
    nmea_fix_t fix;
} nmea_parser_t;

/** Reset parsing state and the merged fix (course_cdeg starts absent). */
void nmea_init(nmea_parser_t *p);

/**
 * Feed one byte. Returns NMEA_EV_RMC/_GGA when a recognised sentence has
 * just validated and been merged into the fix, NMEA_EV_BAD on a checksum
 * mismatch, NMEA_EV_NONE otherwise (mid-sentence, unrecognised type, or
 * discarded garbage/oversize framing).
 */
nmea_event_t nmea_feed(nmea_parser_t *p, char c);

/** The merged latest fix state. Never NULL. */
const nmea_fix_t *nmea_fix(const nmea_parser_t *p);

#endif /* NMEA_H */
