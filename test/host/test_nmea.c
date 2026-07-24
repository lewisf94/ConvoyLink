/* Host tests for nmea (docs/05). Fixture checksums verified independently
 * (XOR of the body between '$' and '*'). */
#include "nmea.h"
#include "tinytest.h"

#include <stddef.h>

static nmea_event_t feed(nmea_parser_t *p, const char *s)
{
    nmea_event_t ev = NMEA_EV_NONE;
    for (const char *c = s; *c != '\0'; c++) {
        ev = nmea_feed(p, *c);
    }
    return ev;
}

TT_TEST(fixture_rmc)
{
    nmea_parser_t p;
    nmea_init(&p);
    nmea_event_t ev =
        feed(&p,
             "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,"
             "003.1,W*6A");
    TT_ASSERT_EQ(ev, NMEA_EV_RMC);

    const nmea_fix_t *f = nmea_fix(&p);
    TT_ASSERT(f->valid);
    TT_ASSERT_EQ(f->lat_e7, 481173000);
    TT_ASSERT_EQ(f->lon_e7, 115166667);
    TT_ASSERT_EQ(f->speed_dm_s, 115);
    TT_ASSERT_EQ(f->course_cdeg, 8440);
    TT_ASSERT_EQ(f->utc_hms, 123519);
}

TT_TEST(fixture_gga)
{
    nmea_parser_t p;
    nmea_init(&p);
    nmea_event_t ev =
        feed(&p, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,"
                 "46.9,M,,*47");
    TT_ASSERT_EQ(ev, NMEA_EV_GGA);

    const nmea_fix_t *f = nmea_fix(&p);
    TT_ASSERT_EQ(f->fix_quality, 3);
    TT_ASSERT_EQ(f->sats, 8);
    TT_ASSERT_EQ(f->hdop_x10, 9);
}

TT_TEST(fixture_southern_hemisphere_rmc)
{
    nmea_parser_t p;
    nmea_init(&p);
    nmea_event_t ev = feed(
        &p, "$GPRMC,081836,A,3751.65,S,14507.36,E,000.0,360.0,130998,011.3,"
            "E*62");
    TT_ASSERT_EQ(ev, NMEA_EV_RMC);

    const nmea_fix_t *f = nmea_fix(&p);
    TT_ASSERT_EQ(f->lat_e7, -378608333);
    TT_ASSERT_EQ(f->lon_e7, 1451226667);
    TT_ASSERT_EQ(f->course_cdeg, 0); /* 360.0 == 0.0 */
    TT_ASSERT_EQ(f->speed_dm_s, 0);
}

TT_TEST(corrupted_checksum_rejected_state_preserved)
{
    nmea_parser_t p;
    nmea_init(&p);
    /* Seed a known, distinct fix first so "state preserved" is meaningful. */
    feed(&p, "$GPRMC,081836,A,3751.65,S,14507.36,E,000.0,360.0,130998,011.3,"
             "E*62");
    const nmea_fix_t before = *nmea_fix(&p);

    /* Fixture 1 with one body digit flipped (038 -> 039); checksum left as
     * the ORIGINAL *6A, so it no longer matches. */
    nmea_event_t ev =
        feed(&p,
             "$GPRMC,123519,A,4807.039,N,01131.000,E,022.4,084.4,230394,"
             "003.1,W*6A");
    TT_ASSERT_EQ(ev, NMEA_EV_BAD);

    const nmea_fix_t *after = nmea_fix(&p);
    TT_ASSERT_MEMEQ(after, &before, sizeof(before));
}

TT_TEST(interleaved_garbage_same_result_as_clean)
{
    const char *clean =
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,"
        "W*6A";

    nmea_parser_t p1;
    nmea_init(&p1);
    nmea_event_t ev1 = feed(&p1, clean);

    nmea_parser_t p2;
    nmea_init(&p2);
    const unsigned char garbage_before[] = {0x00, 0xFF, 0x7E, 'X',
                                            'Y',  '\r', '\n', 0x81};
    for (size_t i = 0; i < sizeof(garbage_before); i++) {
        nmea_feed(&p2, (char)garbage_before[i]);
    }
    nmea_event_t ev2 = feed(&p2, clean);
    const unsigned char garbage_after[] = {'\r', '\n', 0x02, 0x9F};
    for (size_t i = 0; i < sizeof(garbage_after); i++) {
        nmea_feed(&p2, (char)garbage_after[i]);
    }

    TT_ASSERT_EQ(ev1, NMEA_EV_RMC);
    TT_ASSERT_EQ(ev2, NMEA_EV_RMC);
    TT_ASSERT_MEMEQ(nmea_fix(&p1), nmea_fix(&p2), sizeof(nmea_fix_t));
}

TT_TEST(no_fix_sentence_lat_lon_untouched)
{
    nmea_parser_t p;
    nmea_init(&p);
    feed(&p, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,"
             "003.1,W*6A");
    int32_t lat_before = nmea_fix(&p)->lat_e7;
    int32_t lon_before = nmea_fix(&p)->lon_e7;

    nmea_event_t ev = feed(&p, "$GPRMC,120000,V,,,,,,,010126,,*36");
    TT_ASSERT_EQ(ev, NMEA_EV_RMC);

    const nmea_fix_t *f = nmea_fix(&p);
    TT_ASSERT(!f->valid);
    TT_ASSERT_EQ(f->lat_e7, lat_before);
    TT_ASSERT_EQ(f->lon_e7, lon_before);
}

TT_TEST(oversize_run_resets_then_next_sentence_parses)
{
    nmea_parser_t p;
    nmea_init(&p);

    nmea_feed(&p, '$');
    for (int i = 0; i < 200; i++) {
        nmea_feed(&p, (char)('A' + (i % 26))); /* never '$' or '*' */
    }

    nmea_event_t ev =
        feed(&p, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,"
                 "230394,003.1,W*6A");
    TT_ASSERT_EQ(ev, NMEA_EV_RMC);
    TT_ASSERT(nmea_fix(&p)->valid);
    TT_ASSERT_EQ(nmea_fix(&p)->lat_e7, 481173000);
}

TT_TEST(gn_talker_parses_like_gp)
{
    nmea_parser_t p;
    nmea_init(&p);
    nmea_event_t ev =
        feed(&p,
             "$GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,"
             "003.1,W*74");
    TT_ASSERT_EQ(ev, NMEA_EV_RMC);

    const nmea_fix_t *f = nmea_fix(&p);
    TT_ASSERT(f->valid);
    TT_ASSERT_EQ(f->lat_e7, 481173000);
    TT_ASSERT_EQ(f->lon_e7, 115166667);
}

int main(void)
{
    TT_RUN(fixture_rmc);
    TT_RUN(fixture_gga);
    TT_RUN(fixture_southern_hemisphere_rmc);
    TT_RUN(corrupted_checksum_rejected_state_preserved);
    TT_RUN(interleaved_garbage_same_result_as_clean);
    TT_RUN(no_fix_sentence_lat_lon_untouched);
    TT_RUN(oversize_run_resets_then_next_sentence_parses);
    TT_RUN(gn_talker_parses_like_gp);
    return tt_summary("nmea");
}
