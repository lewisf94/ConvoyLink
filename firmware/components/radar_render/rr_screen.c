/* rr_screen.c — see include/radar_scene.h and docs/06-ui.md */
#include "radar_scene.h"

#include "convoy_geo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI_F 3.14159265358979323846f
#define CX 120
#define CY 148

static int round_half_away(float v)
{
    return (v >= 0.0f) ? (int)(v + 0.5f) : (int)(v - 0.5f);
}

void rr_map_to_px(float dx_m, float dy_m, uint16_t scale_m, int *x, int *y)
{
    *x = CX + round_half_away(dx_m * 108.0f / (float)scale_m);
    *y = CY - round_half_away(dy_m * 108.0f / (float)scale_m);
}

uint16_t rr_pick_zoom(float max_live_dist_m)
{
    static const uint16_t scales[] = {250, 500, 1000, 2000, 4000};
    enum { N = sizeof(scales) / sizeof(scales[0]) };
    for (int i = 0; i < N - 1; i++) {
        if (max_live_dist_m <= 0.9f * (float)scales[i]) {
            return scales[i];
        }
    }
    return scales[N - 1];
}

static void fmt_age(uint32_t now_ms, uint32_t heard_ms, char *out, size_t n)
{
    uint32_t age_ms = (uint32_t)(int32_t)(now_ms - heard_ms);
    uint32_t age_s = age_ms / 1000u;
    if (age_s < 60u) {
        snprintf(out, n, "%us", (unsigned)age_s);
    } else {
        snprintf(out, n, "%um", (unsigned)(age_s / 60u));
    }
}

static void clamp_label(int *x, int *y, int w, int h)
{
    if (*x < 0) {
        *x = 0;
    }
    if (*x + w > RR_W) {
        *x = RR_W - w;
    }
    if (*y < 28) {
        *y = 28;
    }
    if (*y + h > 268) {
        *y = 268 - h;
    }
}

static const nt_entry_t *find_neighbor(const rr_scene_t *sc, uint8_t uid)
{
    for (int i = 0; i < sc->n_neighbors; i++) {
        if (sc->neighbors[i].uid == uid) {
            return &sc->neighbors[i];
        }
    }
    return NULL;
}

static void draw_status_bar(rr_fb_t *fb, const rr_scene_t *sc)
{
    char buf[16];

    snprintf(buf, sizeof buf, "U%u %c%c", (unsigned)sc->self_uid,
             sc->self_initials[0], sc->self_initials[1]);
    rr_text(fb, 4, 6, buf, RR_WHITE, 2);

    if (sc->own_fix) {
        snprintf(buf, sizeof buf, "%uo", (unsigned)sc->own_sats);
        int w = 16 * (int)strlen(buf);
        rr_text(fb, CX - w / 2, 6, buf, RR_GREEN, 2);
    } else if (((sc->now_ms / 500u) % 2u) == 0u) {
        const char *nf = "NO FIX";
        rr_text(fb, CX - 8 * (int)strlen(nf) / 2, 9, nf, RR_RED, 1);
    }

    if (sc->ptt_tx) {
        rr_fill_rect(fb, 198, 4, 38, 20, RR_RED);
        rr_text(fb, 198 + (38 - 16) / 2, 7, "TX", RR_WHITE, 2);
    } else if (sc->rx_talker_uid >= 0) {
        const nt_entry_t *tk =
            find_neighbor(sc, (uint8_t)sc->rx_talker_uid);
        if (tk != NULL) {
            /* left-pointing marker: our font has no U+25C2, use '<' */
            rr_text(fb, 182, 7, "<", RR_GREEN, 2);
            char rxbuf[3] = {tk->initials[0], tk->initials[1], '\0'};
            rr_text(fb, 198, 7, rxbuf, RR_GREEN, 2);
        }
    }

    rr_hline(fb, 0, RR_W - 1, 27, RR_RING);
}

static void draw_offscale_arrow(rr_fb_t *fb, const rr_scene_t *sc,
                                const nt_entry_t *n, uint16_t color)
{
    float bearing =
        geo_bearing_deg(sc->own_lat_e7, sc->own_lon_e7, n->last.lat_e7,
                        n->last.lon_e7);
    float rad = bearing * (PI_F / 180.0f);
    float s = sinf(rad), co = cosf(rad);
    float px = co, py = s; /* perpendicular to (s,-co) */

    int tip_x = CX + round_half_away(108.0f * s);
    int tip_y = CY - round_half_away(108.0f * co);
    float bx = (float)CX + 101.0f * s, by = (float)CY - 101.0f * co;
    int b1x = round_half_away(bx + 3.0f * px);
    int b1y = round_half_away(by + 3.0f * py);
    int b2x = round_half_away(bx - 3.0f * px);
    int b2y = round_half_away(by - 3.0f * py);
    rr_tri_filled(fb, tip_x, tip_y, b1x, b1y, b2x, b2y, color);

    int lx = CX + round_half_away(88.0f * s) - 4;
    int ly = CY - round_half_away(88.0f * co) - 4;
    clamp_label(&lx, &ly, 16, 8);
    char init[3] = {n->initials[0], n->initials[1], '\0'};
    rr_text(fb, lx, ly, init, color, 1);
}

static void draw_neighbor_dot(rr_fb_t *fb, const rr_scene_t *sc,
                              const nt_entry_t *n, nt_tier_t tier)
{
    if (n->last.fix_quality == 0) {
        return; /* online, no GPS -> never plotted (docs/05) */
    }

    float dist_m = geo_dist_m(sc->own_lat_e7, sc->own_lon_e7,
                              n->last.lat_e7, n->last.lon_e7);
    uint16_t color = (tier == NT_GHOST) ? RR_GHOST : RR_UNIT_COLOR[n->uid];

    if (dist_m > (float)sc->zoom_scale_m) {
        draw_offscale_arrow(fb, sc, n, color);
        return;
    }

    float dx, dy;
    geo_rel_xy(sc->own_lat_e7, sc->own_lon_e7, n->last.lat_e7,
              n->last.lon_e7, &dx, &dy);
    int x, y;
    rr_map_to_px(dx, dy, sc->zoom_scale_m, &x, &y);

    char init[3] = {n->initials[0], n->initials[1], '\0'};
    char age[8];
    int lx = x + 7, ly = y - 4;

    switch (tier) {
    case NT_LIVE:
        rr_circle(fb, x, y, 4, color, true);
        clamp_label(&lx, &ly, 16, 8);
        rr_text(fb, lx, ly, init, color, 1);
        break;
    case NT_STALE:
        rr_circle(fb, x, y, 2, color, false);
        clamp_label(&lx, &ly, 16, 8);
        rr_text(fb, lx, ly, init, color, 1);
        fmt_age(sc->now_ms, n->last_heard_ms, age, sizeof age);
        rr_text(fb, lx, ly + 9, age, RR_TEXT, 1);
        break;
    case NT_GHOST:
        rr_circle(fb, x, y, 2, RR_GHOST, false);
        clamp_label(&lx, &ly, 16, 8);
        fmt_age(sc->now_ms, n->last_heard_ms, age, sizeof age);
        rr_text(fb, lx, ly, age, RR_GHOST, 1);
        break;
    default:
        break;
    }
}

static void draw_own_marker(rr_fb_t *fb, const rr_scene_t *sc)
{
    if (sc->own_course_cdeg == CL_COURSE_INVALID) {
        rr_circle(fb, CX, CY, 5, RR_WHITE, false);
        return;
    }
    float rad = (float)sc->own_course_cdeg * 0.01f * (PI_F / 180.0f);
    float s = sinf(rad), co = cosf(rad);
    float px = co, py = s;

    int tip_x = CX + round_half_away(5.0f * s);
    int tip_y = CY - round_half_away(5.0f * co);
    float bx = (float)CX - 4.0f * s, by = (float)CY + 4.0f * co;
    int b1x = round_half_away(bx + 4.0f * px);
    int b1y = round_half_away(by + 4.0f * py);
    int b2x = round_half_away(bx - 4.0f * px);
    int b2y = round_half_away(by - 4.0f * py);
    rr_tri_filled(fb, tip_x, tip_y, b1x, b1y, b2x, b2y, RR_WHITE);
}

static void draw_radar_area(rr_fb_t *fb, const rr_scene_t *sc)
{
    rr_circle(fb, CX, CY, 36, RR_RING, false);
    rr_circle(fb, CX, CY, 72, RR_RING, false);
    rr_circle(fb, CX, CY, 108, RR_RING, false);

    char zbuf[8];
    geo_fmt_dist((float)sc->zoom_scale_m, zbuf);
    int zx = CX + (int)(108.0f * 0.7071f) + 3;
    int zy = CY - (int)(108.0f * 0.7071f) - 3;
    rr_text(fb, zx, zy, zbuf, RR_TEXT, 1);

    rr_text(fb, CX - 4, CY - 114 - 4, "N", RR_TEXT, 1);

    if (!sc->own_fix) {
        const char *l1 = "NO FIX";
        const char *l2 = "dots hidden";
        rr_text(fb, CX - 16 * (int)strlen(l1) / 2, CY - 10, l1, RR_RED, 2);
        rr_text(fb, CX - 8 * (int)strlen(l2) / 2, CY + 8, l2, RR_TEXT, 1);
        return; /* no dots, no own marker meaningfully without a fix */
    }

    for (int i = 0; i < sc->n_neighbors; i++) {
        if (nt_tier(&sc->neighbors[i], sc->now_ms) == NT_GHOST) {
            draw_neighbor_dot(fb, sc, &sc->neighbors[i], NT_GHOST);
        }
    }
    for (int i = 0; i < sc->n_neighbors; i++) {
        if (nt_tier(&sc->neighbors[i], sc->now_ms) == NT_STALE) {
            draw_neighbor_dot(fb, sc, &sc->neighbors[i], NT_STALE);
        }
    }
    for (int i = 0; i < sc->n_neighbors; i++) {
        if (nt_tier(&sc->neighbors[i], sc->now_ms) == NT_LIVE) {
            draw_neighbor_dot(fb, sc, &sc->neighbors[i], NT_LIVE);
        }
    }

    draw_own_marker(fb, sc);

    if (sc->ptt_busy) {
        rr_fill_rect(fb, CX - 36, CY - 12, 72, 24, RR_YELLOW);
        rr_text(fb, CX - 16, CY - 8, "BUSY", RR_BG, 2);
    } else if (sc->n_neighbors == 0) {
        const char *w = "waiting for convoy...";
        rr_text(fb, CX - 4 * (int)strlen(w), CY - 4, w, RR_TEXT, 1);
    }
}

static int neighbor_sort_less(const rr_scene_t *sc, const nt_entry_t *a,
                              const nt_entry_t *b)
{
    bool a_unknown = !sc->own_fix || a->last.fix_quality == 0;
    bool b_unknown = !sc->own_fix || b->last.fix_quality == 0;
    if (a_unknown != b_unknown) {
        return a_unknown ? 0 : 1; /* known sorts before unknown */
    }
    if (!a_unknown) {
        float da = geo_dist_m(sc->own_lat_e7, sc->own_lon_e7, a->last.lat_e7,
                              a->last.lon_e7);
        float db = geo_dist_m(sc->own_lat_e7, sc->own_lon_e7, b->last.lat_e7,
                              b->last.lon_e7);
        if (da != db) {
            return da < db;
        }
    }
    return a->uid < b->uid;
}

static void draw_strip(rr_fb_t *fb, const rr_scene_t *sc)
{
    int order[CL_MAX_UNITS];
    int n = sc->n_neighbors;
    for (int i = 0; i < n; i++) {
        order[i] = i;
    }
    /* insertion sort, n <= CL_MAX_UNITS: nearest first, unknown last */
    for (int i = 1; i < n; i++) {
        int cur = order[i];
        int j = i - 1;
        while (j >= 0 && neighbor_sort_less(sc, &sc->neighbors[cur],
                                            &sc->neighbors[order[j]])) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = cur;
    }

    int shown = (n < 4) ? n : 4;
    for (int slot = 0; slot < shown; slot++) {
        const nt_entry_t *e = &sc->neighbors[order[slot]];
        int x0 = 2 + 60 * slot, y0 = 270;
        uint16_t col = RR_UNIT_COLOR[e->uid];

        char init[3] = {e->initials[0], e->initials[1], '\0'};
        rr_text(fb, x0 + 6, y0, init, col, 2);

        char dist_buf[8];
        if (!sc->own_fix) {
            snprintf(dist_buf, sizeof dist_buf, "?");
        } else if (e->last.fix_quality == 0) {
            snprintf(dist_buf, sizeof dist_buf, "--");
        } else {
            float d = geo_dist_m(sc->own_lat_e7, sc->own_lon_e7,
                                 e->last.lat_e7, e->last.lon_e7);
            geo_fmt_dist(d, dist_buf);
        }
        rr_text(fb, x0 + 6, y0 + 20, dist_buf, RR_WHITE, 1);

        nt_tier_t tier = nt_tier(e, sc->now_ms);
        if (tier != NT_LIVE) {
            char age[8];
            fmt_age(sc->now_ms, e->last_heard_ms, age, sizeof age);
            rr_text(fb, x0 + 6, y0 + 32, age, RR_TEXT, 1);
        }

        if (sc->rx_talker_uid >= 0 && (uint8_t)sc->rx_talker_uid == e->uid) {
            rr_fill_rect(fb, x0, y0, 58, 2, RR_GREEN);
            rr_fill_rect(fb, x0, y0 + 46, 58, 2, RR_GREEN);
            rr_fill_rect(fb, x0, y0, 2, 48, RR_GREEN);
            rr_fill_rect(fb, x0 + 56, y0, 2, 48, RR_GREEN);
        }
    }
}

static void draw_provision_banner(rr_fb_t *fb)
{
    const char *l1 = "PROVISION ME";
    rr_text(fb, CX - 8 * (int)strlen(l1), 140, l1, RR_WHITE, 2);
    const char *l2 = "run: unitcfg set <id> <II>";
    rr_text(fb, CX - 4 * (int)strlen(l2), 170, l2, RR_TEXT, 1);
}

void rr_screen_draw(rr_fb_t *fb, const rr_scene_t *sc)
{
    rr_clear(fb, RR_BG);

    if (!sc->provisioned) {
        draw_provision_banner(fb);
        return;
    }

    draw_status_bar(fb, sc);
    draw_radar_area(fb, sc);
    draw_strip(fb, sc);
}
