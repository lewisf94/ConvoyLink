/**
 * scenario — loads a CSV of per-unit GPS waypoints and samples each
 * unit's interpolated position/speed/course at any simulated time
 * (docs/07-dev-guide.md §Simulator).
 *
 * CSV format (header row required, then any number of data rows):
 *   t_ms,unit_id,initials,lat,lon,speed_mps,course_deg
 *
 * Rows for the same unit_id must be in non-decreasing t_ms order. Before
 * a unit's first waypoint it is ABSENT (no fix, doesn't beacon); after
 * its last waypoint it PARKS there (position/course held, speed forced
 * to 0 — it keeps beaconing, just stationary).
 */
#ifndef SIM_SCENARIO_H
#define SIM_SCENARIO_H

#include <stdbool.h>
#include <stdint.h>

#define SCN_MAX_UNITS 5
#define SCN_MAX_WAYPOINTS 1024

typedef struct {
    uint32_t t_ms;
    int32_t lat_e7, lon_e7;
    float speed_mps;
    float course_deg;
} scn_waypoint_t;

typedef struct {
    bool used;
    uint8_t unit_id;
    char initials[2];
    int n_waypoints;
    scn_waypoint_t wp[SCN_MAX_WAYPOINTS];
} scn_track_t;

typedef struct {
    scn_track_t units[SCN_MAX_UNITS];
    uint32_t duration_ms; /* max t_ms across every waypoint */
} scenario_t;

/**
 * Loads and validates the CSV at path (header row required; per-unit rows
 * must be time-ordered; unit_id must be < SCN_MAX_UNITS; initials must be
 * exactly 2 characters). Returns 0 on success, negative on error (a
 * diagnostic is printed to stderr).
 */
int scenario_load(const char *path, scenario_t *out);

typedef struct {
    bool present; /* false = before the unit's first waypoint (absent) */
    int32_t lat_e7, lon_e7;
    float speed_mps;
    float course_deg;
} scn_sample_t;

/** Interpolated/parked/absent state of unit_id at time t_ms. */
void scenario_sample(const scenario_t *scn, uint8_t unit_id, uint32_t t_ms,
                     scn_sample_t *out);

#endif /* SIM_SCENARIO_H */
