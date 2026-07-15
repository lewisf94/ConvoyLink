/* Host tests for convoy_proto — the exemplar test suite (docs/03). */
#include "convoy_proto.h"
#include "tinytest.h"

TT_TEST(struct_sizes_are_wire_format)
{
    TT_ASSERT_EQ(sizeof(cl_hdr_t), 4);
    TT_ASSERT_EQ(sizeof(cl_beacon_t), CL_PKT_SIZE);
    TT_ASSERT_EQ(sizeof(cl_ping_t), CL_PKT_SIZE);
}

TT_TEST(beacon_roundtrip)
{
    cl_beacon_t b;
    cl_make_beacon(&b, 2, 0xBEEF, "LF", 515074000, -11304000, 250, 27050, 3,
                   9, 1234);

    const uint8_t *buf = (const uint8_t *)&b;
    TT_ASSERT_EQ(cl_validate(buf, CL_PKT_SIZE), CL_TYPE_BEACON);
    TT_ASSERT_EQ(cl_type(buf), CL_TYPE_BEACON);

    TT_ASSERT_EQ(b.hdr.magic, 0xC7);
    TT_ASSERT_EQ(b.hdr.ver_type, (CL_PROTO_VER << 4) | CL_TYPE_BEACON);
    TT_ASSERT_EQ(b.hdr.sender, 2);
    TT_ASSERT_EQ(b.hdr.meta, 0); /* hop 0 */
    TT_ASSERT_EQ(b.seq, 0xBEEF);
    TT_ASSERT_MEMEQ(b.initials, "LF", 2);
    TT_ASSERT_EQ(b.lat_e7, 515074000);
    TT_ASSERT_EQ(b.lon_e7, -11304000);
    TT_ASSERT_EQ(b.speed_dm_s, 250);
    TT_ASSERT_EQ(b.course_cdeg, 27050);
    TT_ASSERT_EQ(b.fix_quality, 3);
    TT_ASSERT_EQ(b.sats, 9);
    TT_ASSERT_EQ(b.fix_age_ms, 1234);
    for (size_t i = 0; i < sizeof(b.reserved); i++) {
        TT_ASSERT_EQ(b.reserved[i], 0);
    }
}

TT_TEST(beacon_fix_age_saturates)
{
    cl_beacon_t b;
    cl_make_beacon(&b, 0, 1, "AB", 0, 0, 0, CL_COURSE_INVALID, 0, 0, 999999);
    TT_ASSERT_EQ(b.fix_age_ms, 0xFFFF);
}

TT_TEST(beacon_relay_rewrites_only_hop)
{
    cl_beacon_t b, orig;
    cl_make_beacon(&b, 4, 77, "ZZ", 100, 200, 3, 4, 2, 5, 6);
    orig = b;
    cl_beacon_to_relay(&b);
    TT_ASSERT_EQ(b.hdr.meta, 1);
    b.hdr.meta = orig.hdr.meta;
    TT_ASSERT_MEMEQ(&b, &orig, sizeof(b));
    /* relayed copy still validates */
    cl_beacon_to_relay(&b);
    TT_ASSERT_EQ(cl_validate((uint8_t *)&b, CL_PKT_SIZE), CL_TYPE_BEACON);
}

TT_TEST(reserved_voice_type_rejected)
{
    /* Type 2 carried digital voice before the v2 (SA818 analog) pivot;
     * it is reserved and must never validate. */
    cl_beacon_t b;
    cl_make_beacon(&b, 0, 1, "AB", 0, 0, 0, 0, 3, 5, 0);
    uint8_t buf[CL_PKT_SIZE];
    memcpy(buf, &b, sizeof(b));
    buf[1] = (CL_PROTO_VER << 4) | 0x02;
    TT_ASSERT_EQ(cl_validate(buf, CL_PKT_SIZE), CL_ERR_TYPE);
}

TT_TEST(ping_pattern)
{
    cl_ping_t p;
    cl_make_ping(&p, 3, 9, 123456);
    TT_ASSERT_EQ(cl_validate((uint8_t *)&p, CL_PKT_SIZE), CL_TYPE_PING);
    TT_ASSERT_EQ(p.uptime_ms, 123456);
    for (size_t i = 0; i < sizeof(p.pattern); i++) {
        TT_ASSERT_EQ(p.pattern[i], 0xA5);
    }
}

TT_TEST(validate_rejects_junk)
{
    cl_beacon_t b;
    cl_make_beacon(&b, 0, 1, "AB", 0, 0, 0, 0, 3, 5, 0);
    uint8_t buf[CL_PKT_SIZE];

    memcpy(buf, &b, sizeof(b));
    TT_ASSERT_EQ(cl_validate(buf, CL_PKT_SIZE - 1), CL_ERR_SIZE);
    TT_ASSERT_EQ(cl_validate(NULL, CL_PKT_SIZE), CL_ERR_SIZE);

    memcpy(buf, &b, sizeof(b));
    buf[0] = 0x55; /* magic */
    TT_ASSERT_EQ(cl_validate(buf, CL_PKT_SIZE), CL_ERR_MAGIC);

    memcpy(buf, &b, sizeof(b));
    buf[1] = (2 << 4) | CL_TYPE_BEACON; /* future version */
    TT_ASSERT_EQ(cl_validate(buf, CL_PKT_SIZE), CL_ERR_VERSION);

    memcpy(buf, &b, sizeof(b));
    buf[1] = (CL_PROTO_VER << 4) | 0x0E; /* unknown type */
    TT_ASSERT_EQ(cl_validate(buf, CL_PKT_SIZE), CL_ERR_TYPE);

    memcpy(buf, &b, sizeof(b));
    buf[2] = CL_MAX_UNITS; /* sender out of range */
    TT_ASSERT_EQ(cl_validate(buf, CL_PKT_SIZE), CL_ERR_FIELD);

    memcpy(buf, &b, sizeof(b));
    buf[3] = 2; /* hop > max */
    TT_ASSERT_EQ(cl_validate(buf, CL_PKT_SIZE), CL_ERR_FIELD);
}

TT_TEST(beacon_range_checks)
{
    cl_beacon_t b;
    cl_make_beacon(&b, 0, 1, "AB", 900000001, 0, 0, 0, 3, 5, 0);
    TT_ASSERT_EQ(cl_validate((uint8_t *)&b, CL_PKT_SIZE), CL_ERR_FIELD);
    cl_make_beacon(&b, 0, 1, "AB", 0, 0, 0, 36000, 3, 5, 0);
    TT_ASSERT_EQ(cl_validate((uint8_t *)&b, CL_PKT_SIZE), CL_ERR_FIELD);
    cl_make_beacon(&b, 0, 1, "AB", 0, 0, 0, CL_COURSE_INVALID, 3, 5, 0);
    TT_ASSERT_EQ(cl_validate((uint8_t *)&b, CL_PKT_SIZE), CL_TYPE_BEACON);
}

TT_TEST(seq_newer_wraps)
{
    TT_ASSERT(cl_seq_newer(1, 0));
    TT_ASSERT(!cl_seq_newer(0, 1));
    TT_ASSERT(!cl_seq_newer(5, 5));
    TT_ASSERT(cl_seq_newer(0, 65535));      /* wrap forward       */
    TT_ASSERT(!cl_seq_newer(65535, 0));     /* not backwards      */
    TT_ASSERT(cl_seq_newer(32768, 1));      /* just inside window */
    TT_ASSERT(!cl_seq_newer(32769, 1));     /* just outside       */
}

int main(void)
{
    TT_RUN(struct_sizes_are_wire_format);
    TT_RUN(beacon_roundtrip);
    TT_RUN(beacon_fix_age_saturates);
    TT_RUN(beacon_relay_rewrites_only_hop);
    TT_RUN(reserved_voice_type_rejected);
    TT_RUN(ping_pattern);
    TT_RUN(validate_rejects_junk);
    TT_RUN(beacon_range_checks);
    TT_RUN(seq_newer_wraps);
    return tt_summary("convoy_proto");
}
