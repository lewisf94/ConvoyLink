/* nmea.c — see include/nmea.h and docs/05-gps-geo.md */
#include "nmea.h"

#include <ctype.h>
#include <string.h>

enum { PS_IDLE = 0, PS_BODY, PS_CKSUM1, PS_CKSUM2 };

#define MAX_SENTENCE_CHARS 82
#define MAX_FIELDS 20

void nmea_init(nmea_parser_t *p)
{
    memset(p, 0, sizeof(*p));
    p->state = PS_IDLE;
    p->fix.course_cdeg = 0xFFFF; /* absent until an RMC sets it */
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

static const char *fget(char *const fields[], int nfields, int idx)
{
    static const char empty[] = "";
    return (idx >= 0 && idx < nfields) ? fields[idx] : empty;
}

/* Splits body in place (commas -> NUL), fields[i] point into body. */
static int split_fields(char *body, char *fields[], int max_fields)
{
    int n = 0;
    char *p = body;
    fields[n++] = p;
    while (*p != '\0' && n < max_fields) {
        if (*p == ',') {
            *p = '\0';
            fields[n++] = p + 1;
        }
        p++;
    }
    return n;
}

/* Parses ddmm.mmmmm (deg_digits = 2 for lat, 3 for lon). mm_e5 = minutes
 * scaled to 1e5, fractional digits padded/truncated to exactly 5 places
 * (docs/05 §NMEA parser). Returns false on an empty/malformed field. */
static bool parse_coord(const char *field, int deg_digits, int32_t *deg_out,
                        int32_t *mm_e5_out)
{
    if (field == NULL || field[0] == '\0') {
        return false;
    }
    int i = 0;
    int32_t deg = 0;
    for (int k = 0; k < deg_digits; k++) {
        if (!isdigit((unsigned char)field[i])) {
            return false;
        }
        deg = deg * 10 + (field[i] - '0');
        i++;
    }
    int32_t mm_int = 0;
    for (int k = 0; k < 2; k++) {
        if (!isdigit((unsigned char)field[i])) {
            return false;
        }
        mm_int = mm_int * 10 + (field[i] - '0');
        i++;
    }
    int32_t mm_frac_e5 = 0;
    if (field[i] == '.') {
        i++;
        for (int k = 0; k < 5; k++) {
            if (isdigit((unsigned char)field[i])) {
                mm_frac_e5 = mm_frac_e5 * 10 + (field[i] - '0');
                i++;
            } else {
                mm_frac_e5 = mm_frac_e5 * 10; /* pad with zero */
            }
        }
    }
    *deg_out = deg;
    *mm_e5_out = mm_int * 100000 + mm_frac_e5;
    return true;
}

/* Parses a decimal field (e.g. "022.4") into an integer scaled by
 * 10^scale_digits (fractional digits padded/truncated to scale_digits).
 * Returns false on an empty/malformed field (no digits at all). */
static bool parse_decimal_scaled(const char *field, int scale_digits,
                                 int32_t *out)
{
    if (field == NULL || field[0] == '\0') {
        return false;
    }
    int i = 0;
    int32_t whole = 0;
    bool any_digit = false;
    while (isdigit((unsigned char)field[i])) {
        whole = whole * 10 + (field[i] - '0');
        i++;
        any_digit = true;
    }
    int32_t frac = 0;
    if (field[i] == '.') {
        i++;
        for (int k = 0; k < scale_digits; k++) {
            if (isdigit((unsigned char)field[i])) {
                frac = frac * 10 + (field[i] - '0');
                i++;
                any_digit = true;
            } else {
                frac = frac * 10;
            }
        }
    }
    if (!any_digit) {
        return false;
    }
    int32_t scale = 1;
    for (int k = 0; k < scale_digits; k++) {
        scale *= 10;
    }
    *out = whole * scale + frac;
    return true;
}

static bool parse_leading_int(const char *field, int32_t *out)
{
    if (field == NULL || !isdigit((unsigned char)field[0])) {
        return false;
    }
    int32_t v = 0;
    int i = 0;
    while (isdigit((unsigned char)field[i])) {
        v = v * 10 + (field[i] - '0');
        i++;
    }
    *out = v;
    return true;
}

static nmea_event_t parse_rmc(char *fields[], int nfields, nmea_fix_t *fix)
{
    fix->valid = (fget(fields, nfields, 2)[0] == 'A');

    int32_t deg, mm_e5;
    if (parse_coord(fget(fields, nfields, 3), 2, &deg, &mm_e5)) {
        int32_t lat_e7 = deg * 10000000 + (mm_e5 * 100 + 30) / 60;
        if (fget(fields, nfields, 4)[0] == 'S') {
            lat_e7 = -lat_e7;
        }
        fix->lat_e7 = lat_e7;
    }
    if (parse_coord(fget(fields, nfields, 5), 3, &deg, &mm_e5)) {
        int32_t lon_e7 = deg * 10000000 + (mm_e5 * 100 + 30) / 60;
        if (fget(fields, nfields, 6)[0] == 'W') {
            lon_e7 = -lon_e7;
        }
        fix->lon_e7 = lon_e7;
    }

    int32_t speed_x1000;
    if (parse_decimal_scaled(fget(fields, nfields, 7), 3, &speed_x1000)) {
        /* dm_s = knots * 1852*10/3600 = knots_x1000 * 463 / 90000 */
        fix->speed_dm_s = (uint16_t)((speed_x1000 * 463 + 45000) / 90000);
    }

    int32_t course_x100;
    if (parse_decimal_scaled(fget(fields, nfields, 8), 2, &course_x100)) {
        fix->course_cdeg = (uint16_t)(course_x100 % 36000); /* 360.0 == 0.0 */
    } else {
        fix->course_cdeg = 0xFFFF;
    }

    int32_t hms;
    if (parse_leading_int(fget(fields, nfields, 1), &hms)) {
        fix->utc_hms = (uint32_t)hms;
    }

    return NMEA_EV_RMC;
}

static nmea_event_t parse_gga(char *fields[], int nfields, nmea_fix_t *fix)
{
    int32_t raw_q;
    if (parse_leading_int(fget(fields, nfields, 6), &raw_q)) {
        fix->fix_quality = (raw_q == 0) ? 0 : 3;
    }
    int32_t sats;
    if (parse_leading_int(fget(fields, nfields, 7), &sats)) {
        fix->sats = (uint8_t)sats;
    }
    int32_t hdop_x10;
    if (parse_decimal_scaled(fget(fields, nfields, 8), 1, &hdop_x10)) {
        fix->hdop_x10 = (uint16_t)hdop_x10;
    }
    return NMEA_EV_GGA;
}

static nmea_event_t parse_body(nmea_parser_t *p)
{
    char *fields[MAX_FIELDS];
    int n = split_fields(p->body, fields, MAX_FIELDS);
    if (n < 1) {
        return NMEA_EV_NONE;
    }

    size_t typelen = strlen(fields[0]);
    if (typelen < 3) {
        return NMEA_EV_NONE;
    }
    const char *suffix = fields[0] + (typelen - 3);

    if (strcmp(suffix, "RMC") == 0) {
        return parse_rmc(fields, n, &p->fix);
    }
    if (strcmp(suffix, "GGA") == 0) {
        return parse_gga(fields, n, &p->fix);
    }
    return NMEA_EV_NONE;
}

nmea_event_t nmea_feed(nmea_parser_t *p, char c)
{
    if (c == '$') {
        p->state = PS_BODY;
        p->len = 0;
        p->total = 1;
        p->cksum = 0;
        p->cksum_hi = 0;
        return NMEA_EV_NONE;
    }

    switch (p->state) {
    case PS_IDLE:
        return NMEA_EV_NONE;

    case PS_BODY:
        p->total++;
        if (p->total > MAX_SENTENCE_CHARS) {
            p->state = PS_IDLE;
            return NMEA_EV_NONE;
        }
        if (c == '*') {
            p->state = PS_CKSUM1;
            return NMEA_EV_NONE;
        }
        p->cksum ^= (uint8_t)c;
        if (p->len < sizeof(p->body) - 1) {
            p->body[p->len++] = c;
        } else {
            p->state = PS_IDLE; /* shouldn't happen given the 82-char cap */
        }
        return NMEA_EV_NONE;

    case PS_CKSUM1:
    case PS_CKSUM2: {
        p->total++;
        if (p->total > MAX_SENTENCE_CHARS) {
            p->state = PS_IDLE;
            return NMEA_EV_NONE;
        }
        int nib = hex_nibble(c);
        if (nib < 0) {
            p->state = PS_IDLE; /* malformed framing, not a checksum result */
            return NMEA_EV_NONE;
        }
        if (p->state == PS_CKSUM1) {
            p->cksum_hi = (uint8_t)nib;
            p->state = PS_CKSUM2;
            return NMEA_EV_NONE;
        }
        uint8_t received = (uint8_t)((p->cksum_hi << 4) | (uint8_t)nib);
        p->state = PS_IDLE;
        p->body[p->len] = '\0';
        if (received != p->cksum) {
            return NMEA_EV_BAD;
        }
        return parse_body(p);
    }

    default:
        p->state = PS_IDLE;
        return NMEA_EV_NONE;
    }
}

const nmea_fix_t *nmea_fix(const nmea_parser_t *p)
{
    return &p->fix;
}
