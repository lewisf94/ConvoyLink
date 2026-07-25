/* checks.c — see checks.h. Each check function drives sim_core's replay_to
 * / build_scene over its scenario's whole duration and asserts the
 * expectations hard-coded here (tasks/T08-sim-scenarios.md
 * "--check expectations"); no expectation lives in the CSVs themselves.
 */
#include "checks.h"

#include "convoy_geo.h"
#include "neighbor_table.h"
#include "radar_scene.h"
#include "sim_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK_TICK_MS 1000u

static const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* Prints PASS/FAIL for one assertion and returns cond, so callers can
 * accumulate with `ok &= check(...)` - bitwise, deliberately not
 * short-circuiting, so every assertion always runs and prints. */
static bool check(bool cond, const char *desc)
{
    printf("%s %s\n", cond ? "PASS" : "FAIL", desc);
    return cond;
}

static bool is_unit_dot_color(uint16_t c)
{
    if (c == RR_GHOST) {
        return true;
    }
    for (int u = 0; u < 5; u++) {
        if (c == RR_UNIT_COLOR[u]) {
            return true;
        }
    }
    return false;
}

/* True if any neighbour-dot colour appears inside the radar area (rows
 * 28..267 - status bar and neighbour strip are outside this, see
 * test/host/test_radar_render.c for the same convention). */
static bool radar_area_has_dot(const uint16_t *px)
{
    for (int y = 28; y < 268; y++) {
        for (int x = 0; x < RR_W; x++) {
            if (is_unit_dot_color(px[y * RR_W + x])) {
                return true;
            }
        }
    }
    return false;
}

static bool subsequence_in_order(const nt_tier_t *seq, int n,
                                 const nt_tier_t *pat, int m)
{
    int j = 0;
    for (int i = 0; i < n && j < m; i++) {
        if (seq[i] == pat[j]) {
            j++;
        }
    }
    return j == m;
}

/* split_rejoin: U2 (uid 2) as seen by POV (uid 0) must walk
 * LIVE -> STALE -> GHOST -> ... -> LIVE, and the single-hop relay via U1
 * must keep it LIVE for >= 60s after direct range is first exceeded. */
static int check_split_rejoin(const scenario_t *scn, uint32_t seed,
                              float range_m, float loss_p)
{
    const uint8_t pov = 0, other = 2;
    bool ok = true;

    /* Derive "direct range exceeded" from the scenario track itself
     * (not a hard-coded timestamp) so this stays correct if the CSV's
     * geometry/timing ever changes. */
    uint32_t t_direct_exceeded = UINT32_MAX;
    for (uint32_t t = 0; t <= scn->duration_ms; t += CL_BEACON_PERIOD_MS) {
        scn_sample_t sp, so;
        scenario_sample(scn, pov, t, &sp);
        scenario_sample(scn, other, t, &so);
        if (!sp.present || !so.present) {
            continue;
        }
        if (geo_dist_m(sp.lat_e7, sp.lon_e7, so.lat_e7, so.lon_e7) >
            range_m) {
            t_direct_exceeded = t;
            break;
        }
    }
    if (!check(t_direct_exceeded != UINT32_MAX,
              "split_rejoin: direct range is exceeded at some point")) {
        return 1;
    }

    nt_tier_t transitions[64];
    int n_transitions = 0;
    bool sampled_60s = false, live_60s_after_exceeded = false;

    for (uint32_t t = 0; t <= scn->duration_ms; t += CHECK_TICK_MS) {
        nt_t nt[SCN_MAX_UNITS];
        replay_to(scn, t, seed, range_m, loss_p, nt);

        if (nt[pov].e[other].in_use) {
            nt_tier_t tier = nt_tier(&nt[pov].e[other], t);
            if (n_transitions == 0 || transitions[n_transitions - 1] != tier) {
                if (n_transitions < 64) {
                    transitions[n_transitions++] = tier;
                }
            }
            if (t == t_direct_exceeded + 60000u) {
                sampled_60s = true;
                live_60s_after_exceeded = (tier == NT_LIVE);
            }
        }
    }

    nt_tier_t want[4] = {NT_LIVE, NT_STALE, NT_GHOST, NT_LIVE};
    ok &= check(subsequence_in_order(transitions, n_transitions, want, 4),
               "split_rejoin: U2 tier sequence contains "
               "LIVE->STALE->GHOST->...->LIVE in order");
    ok &= check(sampled_60s && live_60s_after_exceeded,
               "split_rejoin: relay keeps U2 LIVE >=60s after direct "
               "range is exceeded");
    return ok ? 0 : 1;
}

/* no_fix_start: POV (uid 0) has no fix for the first 60s. Radar area must
 * paint no neighbour dots during that window, but the neighbour strip
 * (chips) must already list the already-beaconing neighbours - a real
 * radio receives regardless of the POV's own GPS lock (docs/05). After
 * the fix lands, a dot must appear within one beacon period. */
static int check_no_fix_start(const scenario_t *scn, uint32_t seed,
                              float range_m, float loss_p)
{
    const uint8_t pov = 0;
    bool ok = true;
    static uint16_t px[RR_W * RR_H];

    bool own_fix_false_throughout = true;
    bool radar_clean_throughout = true;
    bool chips_present_throughout = true;

    for (uint32_t t = 0; t < 60000u; t += CHECK_TICK_MS) {
        nt_t nt[SCN_MAX_UNITS];
        replay_to(scn, t, seed, range_m, loss_p, nt);
        rr_scene_t sc;
        build_scene(scn, nt, pov, t, RR_ZOOM_AUTO, &sc);

        if (sc.own_fix) {
            own_fix_false_throughout = false;
            continue;
        }
        if (sc.n_neighbors < 1) {
            chips_present_throughout = false;
        }

        rr_fb_t fb = {px, 0, RR_H};
        rr_screen_draw(&fb, &sc);
        if (radar_area_has_dot(px)) {
            radar_clean_throughout = false;
        }
    }

    ok &= check(own_fix_false_throughout,
               "no_fix_start: own_fix is false for the whole first 60s");
    ok &= check(radar_clean_throughout,
               "no_fix_start: no neighbour dot pixels in the radar area "
               "while own has no fix");
    ok &= check(chips_present_throughout,
               "no_fix_start: neighbour strip still lists chips while "
               "own has no fix");

    nt_t nt_at_fix[SCN_MAX_UNITS];
    replay_to(scn, 60000u, seed, range_m, loss_p, nt_at_fix);
    rr_scene_t sc_at_fix;
    build_scene(scn, nt_at_fix, pov, 60000u, RR_ZOOM_AUTO, &sc_at_fix);
    ok &= check(sc_at_fix.own_fix, "no_fix_start: own_fix is true at t=60000");

    bool dot_seen = false;
    for (uint32_t t = 60000u; t <= 60000u + CL_BEACON_PERIOD_MS;
        t += CHECK_TICK_MS) {
        nt_t nt[SCN_MAX_UNITS];
        replay_to(scn, t, seed, range_m, loss_p, nt);
        rr_scene_t sc;
        build_scene(scn, nt, pov, t, RR_ZOOM_AUTO, &sc);
        rr_fb_t fb = {px, 0, RR_H};
        rr_screen_draw(&fb, &sc);
        if (radar_area_has_dot(px)) {
            dot_seen = true;
            break;
        }
    }
    ok &= check(dot_seen, "no_fix_start: a neighbour dot appears within "
                          "one beacon period after fix");
    return ok ? 0 : 1;
}

/* convoy_cruise: auto-zoom (as seen by POV, uid 0) must stay within
 * {250,500,1000} and change scale <=6 times across the whole run. */
static int check_convoy_cruise(const scenario_t *scn, uint32_t seed,
                               float range_m, float loss_p)
{
    const uint8_t pov = 0;
    bool ok = true;
    bool in_allowed_set = true;
    bool have_last = false;
    uint16_t last_scale = 0;
    int n_changes = 0;

    for (uint32_t t = 0; t <= scn->duration_ms; t += CHECK_TICK_MS) {
        nt_t nt[SCN_MAX_UNITS];
        replay_to(scn, t, seed, range_m, loss_p, nt);
        rr_scene_t sc;
        build_scene(scn, nt, pov, t, RR_ZOOM_AUTO, &sc);

        if (sc.zoom_scale_m != 250 && sc.zoom_scale_m != 500 &&
            sc.zoom_scale_m != 1000) {
            in_allowed_set = false;
        }
        if (!have_last) {
            last_scale = sc.zoom_scale_m;
            have_last = true;
        } else if (sc.zoom_scale_m != last_scale) {
            n_changes++;
            last_scale = sc.zoom_scale_m;
        }
    }

    printf("convoy_cruise: %d auto-zoom changes observed\n", n_changes);
    ok &= check(in_allowed_set,
               "convoy_cruise: auto-zoom never leaves {250,500,1000}");
    ok &= check(n_changes <= 6,
               "convoy_cruise: auto-zoom changes <=6 times total");
    return ok ? 0 : 1;
}

int checks_run(const char *scenario_path, const scenario_t *scn,
              uint32_t seed, float range_m, float loss_p)
{
    const char *base = basename_of(scenario_path);
    printf("=== checks: %s ===\n", base);

    if (strcmp(base, "split_rejoin.csv") == 0) {
        return check_split_rejoin(scn, seed, range_m, loss_p);
    }
    if (strcmp(base, "no_fix_start.csv") == 0) {
        return check_no_fix_start(scn, seed, range_m, loss_p);
    }
    if (strcmp(base, "convoy_cruise.csv") == 0) {
        return check_convoy_cruise(scn, seed, range_m, loss_p);
    }
    fprintf(stderr, "checks: no scripted check for %s\n", base);
    return 1;
}
