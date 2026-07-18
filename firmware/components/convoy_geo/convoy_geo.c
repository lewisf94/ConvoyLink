/* convoy_geo.c — see include/convoy_geo.h and docs/05-gps-geo.md */
#include "convoy_geo.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#define GEO_R_M 6371000.0f
#define GEO_DEG2RAD 0.01745329252f
#define GEO_RAD2DEG 57.29577951f

void geo_rel_xy(int32_t lat_a_e7, int32_t lon_a_e7,
                int32_t lat_b_e7, int32_t lon_b_e7,
                float *dx_m, float *dy_m)
{
    int32_t dlat_e7 = lat_b_e7 - lat_a_e7;
    int32_t dlon_e7 = lon_b_e7 - lon_a_e7;

    float lat_a_rad = (float)lat_a_e7 * 1e-7f * GEO_DEG2RAD;
    float dlat_rad = (float)dlat_e7 * 1e-7f * GEO_DEG2RAD;
    float dlon_rad = (float)dlon_e7 * 1e-7f * GEO_DEG2RAD;

    *dy_m = dlat_rad * GEO_R_M;
    *dx_m = dlon_rad * GEO_R_M * cosf(lat_a_rad);
}

float geo_dist_m(int32_t lat_a_e7, int32_t lon_a_e7,
                 int32_t lat_b_e7, int32_t lon_b_e7)
{
    float dx, dy;
    geo_rel_xy(lat_a_e7, lon_a_e7, lat_b_e7, lon_b_e7, &dx, &dy);
    return hypotf(dx, dy);
}

float geo_bearing_deg(int32_t lat_a_e7, int32_t lon_a_e7,
                      int32_t lat_b_e7, int32_t lon_b_e7)
{
    float dx, dy;
    geo_rel_xy(lat_a_e7, lon_a_e7, lat_b_e7, lon_b_e7, &dx, &dy);
    if (dx == 0.0f && dy == 0.0f) {
        return 0.0f;
    }
    float deg = atan2f(dx, dy) * GEO_RAD2DEG;
    if (deg < 0.0f) {
        deg += 360.0f;
    }
    return deg;
}

void geo_fmt_dist(float m, char out[8])
{
    /* Format into a scratch buffer, then copy bounded into out[8]. The
     * result is <= 7 chars by construction; the bounded copy also makes
     * the write provably safe for the -Werror fortify check (which can't
     * range-analyse the %d arguments) and for any non-finite input. */
    char tmp[16];
    if (m < 1000.0f) {
        snprintf(tmp, sizeof tmp, "%dm", (int)(m + 0.5f));
    } else if (m < 10000.0f) {
        int tenths = (int)(m * 0.01f + 0.5f); /* m/100 == km*10 */
        snprintf(tmp, sizeof tmp, "%d.%dkm", tenths / 10, tenths % 10);
    } else if (m >= 99500.0f) {
        snprintf(tmp, sizeof tmp, "99km+");
    } else {
        snprintf(tmp, sizeof tmp, "%dkm", (int)(m * 0.001f + 0.5f));
    }

    size_t i = 0;
    for (; i < 7 && tmp[i] != '\0'; i++) {
        out[i] = tmp[i];
    }
    out[i] = '\0';
}
