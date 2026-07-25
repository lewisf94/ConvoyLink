/* sim_core.c — see sim_core.h. Extracted from main.c (T07) so checks.c
 * (T08) can drive the identical beacon-replay/scene-building logic the
 * render loops use, without duplicating it.
 */
#include "sim_core.h"

#include "convoy_geo.h"
#include "convoy_proto.h"

#include <stdbool.h>
#include <string.h>

#define MAX_EVENTS 4096

/* ---- tiny deterministic PRNG (xorshift32) --------------------------- */

typedef struct {
    uint32_t s;
} prng_t;

static void prng_seed(prng_t *p, uint32_t seed)
{
    p->s = (seed != 0) ? seed : 1u;
}

static uint32_t prng_next(prng_t *p)
{
    uint32_t x = p->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    p->s = x;
    return x;
}

/* True with probability `p` (0..1). */
static bool prng_chance(prng_t *p, float prob)
{
    if (prob <= 0.0f) {
        return false;
    }
    if (prob >= 1.0f) {
        return true;
    }
    return ((float)(prng_next(p) & 0xFFFFFFu) / (float)0xFFFFFFu) < prob;
}

/* ---- beacon replay ----------------------------------------------------- */

typedef struct {
    uint32_t t_ms;
    uint8_t uid;
} beacon_event_t;

static int collect_events(const scenario_t *scn, uint32_t up_to_ms,
                          beacon_event_t *out, int max_out)
{
    int n = 0;
    for (int u = 0; u < SCN_MAX_UNITS; u++) {
        const scn_track_t *tr = &scn->units[u];
        if (!tr->used) {
            continue;
        }
        uint32_t birth = tr->wp[0].t_ms;
        for (uint32_t t = birth; t <= up_to_ms && n < max_out;
            t += CL_BEACON_PERIOD_MS) {
            out[n].t_ms = t;
            out[n].uid = (uint8_t)u;
            n++;
        }
    }
    /* stable sort by (t_ms, uid); n is small (a few hundred at most) */
    for (int i = 1; i < n; i++) {
        beacon_event_t cur = out[i];
        int j = i - 1;
        while (j >= 0 && (out[j].t_ms > cur.t_ms ||
                          (out[j].t_ms == cur.t_ms && out[j].uid > cur.uid))) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = cur;
    }
    return n;
}

uint16_t course_to_cdeg(float speed_mps, float course_deg)
{
    if (speed_mps < 1.5f) {
        return CL_COURSE_INVALID; /* docs/05 own-course rule */
    }
    return (uint16_t)((int)(course_deg * 100.0f + 0.5f) % 36000);
}

/*
 * Position used for radio range checks even before a unit's own GPS fix
 * lands: real radios listen from power-on, independent of GPS lock
 * (docs/05 "no own fix" is not "not receiving" — the neighbour table and
 * beacon relay are unaffected by own fix state, only the radar's own-
 * relative-position math is). A unit that has appeared in the scenario at
 * all but hasn't reached its first waypoint yet is treated, for range
 * purposes only, as stationary at that first waypoint's position — e.g. a
 * car sitting still while it waits for a cold-start fix.
 */
static void net_position(const scenario_t *scn, uint8_t uid, uint32_t t_ms,
                         int32_t *lat_e7, int32_t *lon_e7)
{
    scn_sample_t s;
    scenario_sample(scn, uid, t_ms, &s);
    if (s.present) {
        *lat_e7 = s.lat_e7;
        *lon_e7 = s.lon_e7;
        return;
    }
    const scn_track_t *tr = &scn->units[uid];
    *lat_e7 = tr->wp[0].lat_e7;
    *lon_e7 = tr->wp[0].lon_e7;
}

void replay_to(const scenario_t *scn, uint32_t sim_time_ms, uint32_t seed,
              float range_m, float loss_p, nt_t nt_out[SCN_MAX_UNITS])
{
    for (int i = 0; i < SCN_MAX_UNITS; i++) {
        nt_init(&nt_out[i], (uint8_t)i);
    }

    static beacon_event_t events[MAX_EVENTS];
    int n = collect_events(scn, sim_time_ms, events, MAX_EVENTS);

    uint16_t seq[SCN_MAX_UNITS] = {0};
    prng_t prng;
    prng_seed(&prng, seed);

    for (int e = 0; e < n; e++) {
        uint8_t x = events[e].uid;
        uint32_t t = events[e].t_ms;

        scn_sample_t sx;
        scenario_sample(scn, x, t, &sx);
        if (!sx.present) {
            continue;
        }

        cl_beacon_t beacon;
        cl_make_beacon(&beacon, x, seq[x]++, scn->units[x].initials, sx.lat_e7,
                       sx.lon_e7, (uint16_t)(sx.speed_mps * 10.0f + 0.5f),
                       course_to_cdeg(sx.speed_mps, sx.course_deg), 3, 9, 0);

        for (int r = 0; r < SCN_MAX_UNITS; r++) {
            if (r == x || !scn->units[r].used) {
                continue; /* r never appears in this scenario at all */
            }
            int32_t r_lat, r_lon;
            net_position(scn, (uint8_t)r, t, &r_lat, &r_lon);

            float dist_xr = geo_dist_m(sx.lat_e7, sx.lon_e7, r_lat, r_lon);
            bool delivered = false;
            if (dist_xr <= range_m && !prng_chance(&prng, loss_p)) {
                nt_update_from_beacon(&nt_out[r], &beacon, t);
                delivered = true;
            }
            if (delivered) {
                continue;
            }

            /* single-hop relay: any third present unit in range of both */
            bool relay_ok = false;
            for (int y = 0; y < SCN_MAX_UNITS && !relay_ok; y++) {
                if (y == x || y == r) {
                    continue;
                }
                scn_sample_t sy;
                scenario_sample(scn, (uint8_t)y, t, &sy);
                if (!sy.present) {
                    continue;
                }
                float d_xy =
                    geo_dist_m(sx.lat_e7, sx.lon_e7, sy.lat_e7, sy.lon_e7);
                float d_yr = geo_dist_m(sy.lat_e7, sy.lon_e7, r_lat, r_lon);
                relay_ok = (d_xy <= range_m && d_yr <= range_m);
            }
            if (relay_ok && !prng_chance(&prng, loss_p)) {
                cl_beacon_t relay = beacon;
                cl_beacon_to_relay(&relay);
                nt_update_from_beacon(&nt_out[r], &relay, t);
            }
        }
    }
}

/* ---- scene building ----------------------------------------------------- */

void build_scene(const scenario_t *scn, nt_t nt[SCN_MAX_UNITS], uint8_t pov,
                 uint32_t sim_time_ms, rr_zoom_t zoom_mode, rr_scene_t *sc)
{
    memset(sc, 0, sizeof(*sc));
    sc->provisioned = true;
    sc->self_uid = pov;
    if (scn->units[pov].used) {
        sc->self_initials[0] = scn->units[pov].initials[0];
        sc->self_initials[1] = scn->units[pov].initials[1];
    } else {
        sc->self_initials[0] = '?';
        sc->self_initials[1] = '?';
    }

    scn_sample_t own;
    scenario_sample(scn, pov, sim_time_ms, &own);
    sc->own_fix = own.present;
    sc->own_course_cdeg = CL_COURSE_INVALID;
    if (own.present) {
        sc->own_lat_e7 = own.lat_e7;
        sc->own_lon_e7 = own.lon_e7;
        sc->own_sats = 9;
        sc->own_course_cdeg = course_to_cdeg(own.speed_mps, own.course_deg);
    }

    sc->now_ms = sim_time_ms;
    sc->rx_talker_uid = -1;

    sc->n_neighbors = nt_snapshot(&nt[pov], sc->neighbors, sim_time_ms);

    sc->zoom_mode = zoom_mode;
    if (zoom_mode == RR_ZOOM_AUTO) {
        float max_dist = 0.0f;
        if (sc->own_fix) {
            for (int i = 0; i < sc->n_neighbors; i++) {
                nt_tier_t tier = nt_tier(&sc->neighbors[i], sim_time_ms);
                if ((tier == NT_LIVE || tier == NT_STALE) &&
                    sc->neighbors[i].last.fix_quality != 0) {
                    float d = geo_dist_m(sc->own_lat_e7, sc->own_lon_e7,
                                        sc->neighbors[i].last.lat_e7,
                                        sc->neighbors[i].last.lon_e7);
                    if (d > max_dist) {
                        max_dist = d;
                    }
                }
            }
        }
        sc->zoom_scale_m = rr_pick_zoom(max_dist);
    } else {
        static const uint16_t fixed_scale[] = {0, 250, 500, 1000, 2000, 4000};
        sc->zoom_scale_m = fixed_scale[zoom_mode];
    }
}

void rgb565_to_rgb888(const uint16_t *px, uint8_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        uint16_t v = px[i];
        uint8_t r5 = (uint8_t)((v >> 11) & 0x1Fu);
        uint8_t g6 = (uint8_t)((v >> 5) & 0x3Fu);
        uint8_t b5 = (uint8_t)(v & 0x1Fu);
        out[i * 3 + 0] = (uint8_t)((r5 * 255 + 15) / 31);
        out[i * 3 + 1] = (uint8_t)((g6 * 255 + 31) / 63);
        out[i * 3 + 2] = (uint8_t)((b5 * 255 + 15) / 31);
    }
}
