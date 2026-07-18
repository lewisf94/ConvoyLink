/**
 * convoy_geo — geodesy helpers: relative position, distance, bearing
 * between two GPS fixes, and a human-readable distance formatter.
 *
 * Pure C component: no ESP-IDF headers, no allocation, host-testable.
 * Convoy-scale separations (< ~50 km) use an equirectangular
 * approximation, single-precision float, Earth radius 6 371 000 m
 * (docs/05-gps-geo.md).
 */
#ifndef CONVOY_GEO_H
#define CONVOY_GEO_H

#include <stdint.h>

/**
 * Relative position of B as seen from A, metres. +x = East, +y = North.
 *
 * PRECISION: integer deltas (dlat_e7, dlon_e7) are computed first; only
 * the small delta is converted to float. Converting the absolute e7
 * coordinates to float before subtracting loses the metre-scale delta to
 * rounding error (docs/05 §Geodesy). The east-scaling cosine uses the
 * latitude of A.
 */
void geo_rel_xy(int32_t lat_a_e7, int32_t lon_a_e7,
                int32_t lat_b_e7, int32_t lon_b_e7,
                float *dx_m, float *dy_m);

/** Straight-line distance from A to B, metres (hypot of geo_rel_xy). */
float geo_dist_m(int32_t lat_a_e7, int32_t lon_a_e7,
                 int32_t lat_b_e7, int32_t lon_b_e7);

/**
 * Bearing from A to B, degrees [0, 360), 0 = North, 90 = East.
 * Returns 0.0f when A and B are the same point.
 */
float geo_bearing_deg(int32_t lat_a_e7, int32_t lon_a_e7,
                      int32_t lat_b_e7, int32_t lon_b_e7);

/**
 * Human-readable distance: "0m".."999m", "1.0km".."9.9km", "10km".."99km",
 * or "99km+" for m >= 99500. Always NUL-terminated; out must hold 8 bytes.
 */
void geo_fmt_dist(float m, char out[8]);

#endif /* CONVOY_GEO_H */
