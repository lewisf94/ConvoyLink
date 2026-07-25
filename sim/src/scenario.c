/* scenario.c — see scenario.h */
#include "scenario.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t deg_to_e7(double deg)
{
    return (int32_t)(deg * 1e7 + (deg >= 0.0 ? 0.5 : -0.5));
}

int scenario_load(const char *path, scenario_t *out)
{
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "scenario: cannot open %s\n", path);
        return -1;
    }

    char line[256];
    int lineno = 0;
    if (fgets(line, sizeof line, f) == NULL) {
        fprintf(stderr, "scenario: %s is empty (missing header row)\n", path);
        fclose(f);
        return -1;
    }
    lineno++;

    while (fgets(line, sizeof line, f) != NULL) {
        lineno++;
        /* skip blank lines */
        char *p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0' || *p == '\r' || *p == '\n') {
            continue;
        }

        long t_ms_l, unit_id_l;
        char initials[8] = {0};
        double lat, lon, speed, course;
        int n = sscanf(line, "%ld,%ld,%7[^,],%lf,%lf,%lf,%lf", &t_ms_l,
                       &unit_id_l, initials, &lat, &lon, &speed, &course);
        if (n != 7) {
            fprintf(stderr, "scenario: %s:%d: malformed row (%d/7 fields)\n",
                   path, lineno, n);
            fclose(f);
            return -1;
        }
        if (unit_id_l < 0 || unit_id_l >= SCN_MAX_UNITS) {
            fprintf(stderr, "scenario: %s:%d: unit_id %ld out of range\n",
                   path, lineno, unit_id_l);
            fclose(f);
            return -1;
        }
        if (strlen(initials) != 2) {
            fprintf(stderr,
                   "scenario: %s:%d: initials must be exactly 2 chars\n",
                   path, lineno);
            fclose(f);
            return -1;
        }

        scn_track_t *tr = &out->units[unit_id_l];
        if (tr->n_waypoints >= SCN_MAX_WAYPOINTS) {
            fprintf(stderr, "scenario: %s:%d: unit %ld exceeds %d waypoints\n",
                   path, lineno, unit_id_l, SCN_MAX_WAYPOINTS);
            fclose(f);
            return -1;
        }
        if (tr->n_waypoints > 0 &&
            (uint32_t)t_ms_l < tr->wp[tr->n_waypoints - 1].t_ms) {
            fprintf(stderr,
                   "scenario: %s:%d: unit %ld waypoints not time-ordered\n",
                   path, lineno, unit_id_l);
            fclose(f);
            return -1;
        }

        tr->used = true;
        tr->unit_id = (uint8_t)unit_id_l;
        tr->initials[0] = initials[0];
        tr->initials[1] = initials[1];
        scn_waypoint_t *wp = &tr->wp[tr->n_waypoints++];
        wp->t_ms = (uint32_t)t_ms_l;
        wp->lat_e7 = deg_to_e7(lat);
        wp->lon_e7 = deg_to_e7(lon);
        wp->speed_mps = (float)speed;
        wp->course_deg = (float)course;

        if (wp->t_ms > out->duration_ms) {
            out->duration_ms = wp->t_ms;
        }
    }

    fclose(f);

    for (int i = 0; i < SCN_MAX_UNITS; i++) {
        if (out->units[i].used && out->units[i].n_waypoints < 1) {
            fprintf(stderr, "scenario: unit %d has no waypoints\n", i);
            return -1;
        }
    }
    return 0;
}

void scenario_sample(const scenario_t *scn, uint8_t unit_id, uint32_t t_ms,
                     scn_sample_t *out)
{
    memset(out, 0, sizeof(*out));
    if (unit_id >= SCN_MAX_UNITS) {
        return;
    }
    const scn_track_t *tr = &scn->units[unit_id];
    if (!tr->used || tr->n_waypoints < 1) {
        return; /* never appears in this scenario -> permanently absent */
    }

    if (t_ms < tr->wp[0].t_ms) {
        out->present = false; /* before first waypoint: absent */
        return;
    }

    const scn_waypoint_t *last = &tr->wp[tr->n_waypoints - 1];
    if (t_ms >= last->t_ms) {
        out->present = true; /* after last waypoint: parked */
        out->lat_e7 = last->lat_e7;
        out->lon_e7 = last->lon_e7;
        out->speed_mps = 0.0f; /* parked = stopped */
        out->course_deg = last->course_deg;
        return;
    }

    /* find the [i, i+1] segment containing t_ms (tiny arrays, linear scan) */
    int i = 0;
    while (i + 1 < tr->n_waypoints && tr->wp[i + 1].t_ms <= t_ms) {
        i++;
    }
    const scn_waypoint_t *a = &tr->wp[i], *b = &tr->wp[i + 1];
    uint32_t dt = b->t_ms - a->t_ms;
    uint32_t elapsed = t_ms - a->t_ms;

    out->present = true;
    if (dt == 0) {
        out->lat_e7 = a->lat_e7;
        out->lon_e7 = a->lon_e7;
        out->speed_mps = a->speed_mps;
        out->course_deg = a->course_deg;
        return;
    }
    int64_t dlat = (int64_t)b->lat_e7 - a->lat_e7;
    int64_t dlon = (int64_t)b->lon_e7 - a->lon_e7;
    out->lat_e7 = a->lat_e7 + (int32_t)(dlat * elapsed / dt);
    out->lon_e7 = a->lon_e7 + (int32_t)(dlon * elapsed / dt);

    double frac = (double)elapsed / (double)dt;
    out->speed_mps = a->speed_mps + (float)((b->speed_mps - a->speed_mps) * frac);
    out->course_deg =
        a->course_deg + (float)((b->course_deg - a->course_deg) * frac);
}
