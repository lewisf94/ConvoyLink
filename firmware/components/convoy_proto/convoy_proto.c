/* convoy_proto.c — see include/convoy_proto.h and docs/03-radio-protocol.md */
#include "convoy_proto.h"

#include <string.h>

static void fill_hdr(cl_hdr_t *h, cl_type_t type, uint8_t sender, uint8_t meta)
{
    h->magic = CL_MAGIC;
    h->ver_type = (uint8_t)((CL_PROTO_VER << 4) | (type & 0x0Fu));
    h->sender = sender;
    h->meta = meta;
}

int cl_validate(const uint8_t *buf, size_t len)
{
    if (buf == NULL || len != CL_PKT_SIZE) {
        return CL_ERR_SIZE;
    }
    const cl_hdr_t *h = (const cl_hdr_t *)buf;
    if (h->magic != CL_MAGIC) {
        return CL_ERR_MAGIC;
    }
    if ((h->ver_type >> 4) != CL_PROTO_VER) {
        return CL_ERR_VERSION;
    }
    if (h->sender >= CL_MAX_UNITS) {
        return CL_ERR_FIELD;
    }

    cl_type_t type = (cl_type_t)(h->ver_type & 0x0Fu);
    switch (type) {
    case CL_TYPE_BEACON: {
        const cl_beacon_t *b = (const cl_beacon_t *)buf;
        if (h->meta > CL_BEACON_HOP_MAX) {
            return CL_ERR_FIELD;
        }
        if (b->course_cdeg != CL_COURSE_INVALID && b->course_cdeg > 35999u) {
            return CL_ERR_FIELD;
        }
        if (b->lat_e7 < -900000000 || b->lat_e7 > 900000000 ||
            b->lon_e7 < -1800000000 || b->lon_e7 > 1800000000) {
            return CL_ERR_FIELD;
        }
        break;
    }
    case CL_TYPE_PING:
        break;
    default:
        return CL_ERR_TYPE;
    }
    return (int)type;
}

void cl_make_beacon(cl_beacon_t *out, uint8_t sender, uint16_t seq,
                    const char initials[2], int32_t lat_e7, int32_t lon_e7,
                    uint16_t speed_dm_s, uint16_t course_cdeg,
                    uint8_t fix_quality, uint8_t sats, uint32_t fix_age_ms)
{
    memset(out, 0, sizeof(*out));
    fill_hdr(&out->hdr, CL_TYPE_BEACON, sender, /*hop=*/0);
    out->seq = seq;
    out->initials[0] = initials[0];
    out->initials[1] = initials[1];
    out->lat_e7 = lat_e7;
    out->lon_e7 = lon_e7;
    out->speed_dm_s = speed_dm_s;
    out->course_cdeg = course_cdeg;
    out->fix_quality = fix_quality;
    out->sats = sats;
    out->fix_age_ms = (fix_age_ms > 0xFFFFu) ? 0xFFFFu : (uint16_t)fix_age_ms;
}

void cl_beacon_to_relay(cl_beacon_t *b)
{
    /* Same origin sender/seq/payload; only the hop marker changes. */
    b->hdr.meta = 1u;
}

void cl_make_ping(cl_ping_t *out, uint8_t sender, uint16_t seq,
                  uint32_t uptime_ms)
{
    fill_hdr(&out->hdr, CL_TYPE_PING, sender, 0);
    out->seq = seq;
    out->uptime_ms = uptime_ms;
    memset(out->pattern, 0xA5, sizeof(out->pattern));
}
