# T01 — `convoy_geo`: geodesy math component

**Depends:** none · **Phase:** M1 (pure C)
**Required reading:** `docs/05-gps-geo.md` §Geodesy; `/CLAUDE.md`

## Goal

Convert pairs of GPS positions (int32 degrees×1e7, as carried by
`cl_beacon_t`) into local metres/bearing/distance for the radar, plus the
human distance formatter.

## Deliverables

- `firmware/components/convoy_geo/include/convoy_geo.h`
- `firmware/components/convoy_geo/convoy_geo.c`
- `firmware/components/convoy_geo/CMakeLists.txt` (copy convoy_proto's pattern)
- `test/host/test_convoy_geo.c` (Makefile already wires `SRCS_test_convoy_geo`)

## Interface contract

```c
#include <stdint.h>

/* Relative position of B as seen from A, metres. +x = East, +y = North.
 * Equirectangular approximation, R = 6371000 m, single-precision floats.
 * PRECISION RULE: subtract the e7 integers FIRST, convert the small deltas
 * to float after (docs/05 explains why). cos() uses the latitude of A. */
void geo_rel_xy(int32_t lat_a_e7, int32_t lon_a_e7,
                int32_t lat_b_e7, int32_t lon_b_e7,
                float *dx_m, float *dy_m);

float geo_dist_m(int32_t lat_a_e7, int32_t lon_a_e7,
                 int32_t lat_b_e7, int32_t lon_b_e7);

/* Bearing from A to B, degrees [0, 360), 0 = North, 90 = East.
 * atan2f(dx, dy); returns 0.0f when A == B. */
float geo_bearing_deg(int32_t lat_a_e7, int32_t lon_a_e7,
                      int32_t lat_b_e7, int32_t lon_b_e7);

/* "85m" | "850m" | "2.3km" | "12km" | "99km+" (>= 99500 m). Rounds to
 * nearest unit shown; < 1000 m integer metres; 1–9.9 km one decimal;
 * >= 10 km integer km. out is always NUL-terminated, max 7 chars + NUL. */
void geo_fmt_dist(float m, char out[8]);
```

## Test requirements (all must be present)

1. **Reference cross-check**: implement a `double`-precision equirectangular
   reference *inside the test file* and compare `geo_rel_xy` against it for
   ~20 point pairs around lat 51.5°, lat −37.8°, and lat 0°, separations
   10 m – 20 km. Tolerance: 0.2 % or 0.5 m, whichever is larger.
2. **Fixtures**: 0.0009° north at same lon → dy ≈ +100.08 m, dx ≈ 0;
   pure-east offsets scale by cos(lat) (check one at 51.5°N ≈ ×0.6225).
3. **Precision trap**: A=(515074000, −1278000), B 45 e7-units north
   (≈5.0 m): distance must be 4.5–5.5 m. (Naive float conversion of the
   absolute values fails this.)
4. **Bearings**: N/E/S/W cardinal cases → 0/90/180/270 (±0.5°); a NW case
   in (270, 360); A==B → 0.
5. **Formatter**: 0→"0m", 84.6→"85m", 999.4→"999m", 1000→"1.0km",
   2345→"2.3km", 9949→"9.9km", 10500→"11km" (define: `%.1f`-style rounding),
   123456→"99km+". No buffer overrun (ASan enforces).

## Acceptance — CI

`make -C test/host test` green (new suite runs under ASan/UBSan).

## Out of scope

Neighbour bookkeeping (T04), screen mapping (T06), any ESP-IDF code.
