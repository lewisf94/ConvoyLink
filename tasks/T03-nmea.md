# T03 — `nmea`: NMEA 0183 parser component

**Depends:** none · **Phase:** M1 (pure C)
**Required reading:** `docs/05-gps-geo.md` §NMEA parser (the field table and
parsing rules there are binding)

## Goal

Byte-at-a-time NMEA parser producing integer-only fixes from RMC + GGA,
robust to garbage, fragmentation and empty fields.

## Deliverables

- `firmware/components/nmea/include/nmea.h`
- `firmware/components/nmea/nmea.c`
- `firmware/components/nmea/CMakeLists.txt`
- `test/host/test_nmea.c`

## Interface contract

```c
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     valid;        /* RMC status 'A' seen (cleared by 'V')      */
    int32_t  lat_e7, lon_e7;
    uint16_t speed_dm_s;   /* from knots: round(kn * 5.14444)           */
    uint16_t course_cdeg;  /* 0xFFFF when field empty                   */
    uint8_t  fix_quality;  /* GGA: 0 stays 0, >=1 maps to 3             */
    uint8_t  sats;
    uint16_t hdop_x10;
    uint32_t utc_hms;      /* hhmmss integer, logging only              */
} nmea_fix_t;

typedef enum { NMEA_EV_NONE = 0, NMEA_EV_RMC, NMEA_EV_GGA, NMEA_EV_BAD } nmea_event_t;

typedef struct { /* implementation-defined, <= 128 bytes, no pointers */ } nmea_parser_t;

void nmea_init(nmea_parser_t *p);
nmea_event_t nmea_feed(nmea_parser_t *p, char c);   /* event fires on sentence end */
const nmea_fix_t *nmea_fix(const nmea_parser_t *p); /* merged latest state */
```

Binding rules (from docs/05): checksum verified before any field is applied;
match sentence type on the last 3 letters (any talker: GP/GN/GL…);
coordinate conversion in **integer/int64 math only** with round-half-up:
`e7 = deg*10000000 + (mm_e5*100 + 30)/60` where `mm_e5` is minutes ×1e5
(pad/truncate input decimals to 5 places); S/W negate; sentences longer
than 82 chars reset the state machine; empty fields leave prior values
untouched except `valid`/`course_cdeg` as specified.

## Test requirements

Fixtures (exact expected values — these are precomputed, don't re-derive):

1. `$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A`
   → RMC event, valid=true, lat_e7=**481173000**, lon_e7=**115166667**,
   speed_dm_s=**115**, course_cdeg=**8440**, utc_hms=123519.
2. `$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47`
   → GGA event, fix_quality=3, sats=8, hdop_x10=9.
3. `$GPRMC,081836,A,3751.65,S,14507.36,E,000.0,360.0,130998,011.3,E*62`
   → lat_e7=**−378608333**, lon_e7=**1451226667**, course_cdeg=36000 is
   out of range 0..35999 → store 0 (360.0 ≡ 0.0), speed 0.
4. Corrupted copy of (1) with one flipped char → `NMEA_EV_BAD`, fix
   unchanged from before.
5. Feed fixture (1) one char at a time interleaved with random binary
   garbage between sentences → same result as clean feed.
6. No-fix sentence `$GPRMC,120000,V,,,,,,,010126,,*<correct XX>` (compute
   the checksum in the test) → RMC event, valid=false, lat/lon untouched.
7. 200-char run without `\n` → parser resets, next good sentence parses.
8. GN talker: fixture (1) rewritten `$GNRMC,…` with recomputed checksum
   parses identically.

## Acceptance — CI

`make -C test/host test` green.

## Out of scope

UART plumbing (T11), UBX configuration, sentences other than RMC/GGA.
