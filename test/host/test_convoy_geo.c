/* Host tests for convoy_geo (docs/05). */
#include "convoy_geo.h"
#include "tinytest.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define REF_R 6371000.0
#define REF_DEG2RAD (3.14159265358979323846 / 180.0)

/* Double-precision reference implementation of the SAME equirectangular
 * approximation used by convoy_geo — this catches precision-loss bugs in
 * the float version (it is not a true-geodesic oracle; the approximation
 * itself is the spec, per docs/05). */
static void ref_rel_xy(double lat_a_deg, double lon_a_deg,
                      double lat_b_deg, double lon_b_deg,
                      double *dx, double *dy)
{
    double dlat = (lat_b_deg - lat_a_deg) * REF_DEG2RAD;
    double dlon = (lon_b_deg - lon_a_deg) * REF_DEG2RAD;
    *dy = dlat * REF_R;
    *dx = dlon * REF_R * cos(lat_a_deg * REF_DEG2RAD);
}

static int32_t deg_to_e7(double deg)
{
    return (int32_t)(deg * 1e7 + (deg >= 0 ? 0.5 : -0.5));
}

TT_TEST(cross_check_against_double_reference)
{
    const double base_lats[] = {51.5, -37.8, 0.0};
    /* offsets in degrees spanning ~10 m to ~20 km at these latitudes */
    const double offs[] = {0.0001,  -0.0002, 0.001, -0.0015, 0.01,
                           -0.02,    0.05,    -0.08, 0.12,   -0.18};
    const size_t n_off = sizeof(offs) / sizeof(offs[0]);

    for (size_t bi = 0; bi < sizeof(base_lats) / sizeof(base_lats[0]); bi++) {
        double lat_a = base_lats[bi];
        double lon_a = 10.0; /* arbitrary non-zero base longitude */

        for (size_t oi = 0; oi < n_off; oi++) {
            double lat_b = lat_a + offs[oi];
            double lon_b = lon_a + offs[(oi + 3) % n_off];

            int32_t lat_a_e7 = deg_to_e7(lat_a), lon_a_e7 = deg_to_e7(lon_a);
            int32_t lat_b_e7 = deg_to_e7(lat_b), lon_b_e7 = deg_to_e7(lon_b);

            double rdx, rdy;
            ref_rel_xy(lat_a, lon_a, lat_b, lon_b, &rdx, &rdy);

            float dx, dy;
            geo_rel_xy(lat_a_e7, lon_a_e7, lat_b_e7, lon_b_e7, &dx, &dy);

            double tol_x = fmax(0.5, fabs(rdx) * 0.002);
            double tol_y = fmax(0.5, fabs(rdy) * 0.002);
            TT_ASSERT_NEAR(dx, rdx, tol_x);
            TT_ASSERT_NEAR(dy, rdy, tol_y);
        }
    }
}

TT_TEST(fixture_north_offset)
{
    int32_t lat_a = 515074000, lon_a = -1278000;
    int32_t lat_b = lat_a + 9000; /* 0.0009 deg * 1e7 */
    float dx, dy;
    geo_rel_xy(lat_a, lon_a, lat_b, lon_a, &dx, &dy);
    TT_ASSERT_NEAR(dy, 100.08, 0.5);
    TT_ASSERT_NEAR(dx, 0.0, 0.001);
}

TT_TEST(fixture_east_offset_scales_by_cos_lat)
{
    /* 0.01 deg east at 51.5N vs at the equator; ratio should be cos(51.5deg). */
    int32_t lat0 = 0, lon0 = 0;
    int32_t lat51 = 515000000;
    int32_t dlon_e7 = 100000; /* 0.01 deg */

    float dx_eq, dy_eq, dx_51, dy_51;
    geo_rel_xy(lat0, lon0, lat0, lon0 + dlon_e7, &dx_eq, &dy_eq);
    geo_rel_xy(lat51, lon0, lat51, lon0 + dlon_e7, &dx_51, &dy_51);

    double expect_ratio = cos(51.5 * REF_DEG2RAD); /* ~0.6225 */
    TT_ASSERT_NEAR((double)(dx_51 / dx_eq), expect_ratio, 0.001);
    TT_ASSERT_NEAR(dy_51, 0.0, 0.001);
    TT_ASSERT_NEAR(dy_eq, 0.0, 0.001);
}

TT_TEST(precision_trap_large_absolute_coords)
{
    /* The trap: a tiny offset against large absolute e7 coords. Converting
     * the absolute values to float BEFORE subtracting loses the delta
     * (float has ~7 sig digits; 5.15e8 needs 9), collapsing distance to ~0.
     * geo_geo subtracts as int32 first, so it resolves correctly.
     * NB: the T01 spec said "45 e7-units (~5.0 m)"; 45 e7-units is actually
     * ~0.5 m (450 e7-units is ~5 m) — flagged in STATUS. We check both. */
    int32_t lat_a = 515074000, lon_a = -1278000;

    float d5 = geo_dist_m(lat_a, lon_a, lat_a + 450, lon_a); /* ~5.0 m */
    TT_ASSERT(d5 >= 4.5f && d5 <= 5.5f);

    float d05 = geo_dist_m(lat_a, lon_a, lat_a + 45, lon_a); /* ~0.5 m */
    TT_ASSERT(d05 >= 0.4f && d05 <= 0.6f);
}

TT_TEST(bearings_cardinal_and_identity)
{
    int32_t lat_a = 500000000, lon_a = 0;

    TT_ASSERT_NEAR(geo_bearing_deg(lat_a, lon_a, lat_a + 1000, lon_a), 0.0, 0.5);
    TT_ASSERT_NEAR(geo_bearing_deg(lat_a, lon_a, lat_a, lon_a + 1000), 90.0, 0.5);
    TT_ASSERT_NEAR(geo_bearing_deg(lat_a, lon_a, lat_a - 1000, lon_a), 180.0, 0.5);
    TT_ASSERT_NEAR(geo_bearing_deg(lat_a, lon_a, lat_a, lon_a - 1000), 270.0, 0.5);

    float nw = geo_bearing_deg(lat_a, lon_a, lat_a + 1000, lon_a - 1000);
    TT_ASSERT(nw > 270.0f && nw < 360.0f);

    TT_ASSERT_NEAR(geo_bearing_deg(lat_a, lon_a, lat_a, lon_a), 0.0, 0.001);
}

TT_TEST(formatter_fixtures)
{
    char buf[8];

    geo_fmt_dist(0.0f, buf);
    TT_ASSERT(strcmp(buf, "0m") == 0);

    geo_fmt_dist(84.6f, buf);
    TT_ASSERT(strcmp(buf, "85m") == 0);

    geo_fmt_dist(999.4f, buf);
    TT_ASSERT(strcmp(buf, "999m") == 0);

    geo_fmt_dist(1000.0f, buf);
    TT_ASSERT(strcmp(buf, "1.0km") == 0);

    geo_fmt_dist(2345.0f, buf);
    TT_ASSERT(strcmp(buf, "2.3km") == 0);

    geo_fmt_dist(9949.0f, buf);
    TT_ASSERT(strcmp(buf, "9.9km") == 0);

    geo_fmt_dist(10500.0f, buf);
    TT_ASSERT(strcmp(buf, "11km") == 0);

    geo_fmt_dist(123456.0f, buf);
    TT_ASSERT(strcmp(buf, "99km+") == 0);
}

TT_TEST(formatter_no_overrun_on_huge_values)
{
    char buf[8];
    geo_fmt_dist(999999999.0f, buf);
    TT_ASSERT(strlen(buf) <= 7);
    TT_ASSERT(strcmp(buf, "99km+") == 0);
}

int main(void)
{
    TT_RUN(cross_check_against_double_reference);
    TT_RUN(fixture_north_offset);
    TT_RUN(fixture_east_offset_scales_by_cos_lat);
    TT_RUN(precision_trap_large_absolute_coords);
    TT_RUN(bearings_cardinal_and_identity);
    TT_RUN(formatter_fixtures);
    TT_RUN(formatter_no_overrun_on_huge_values);
    return tt_summary("convoy_geo");
}
