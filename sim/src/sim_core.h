/**
 * sim_core — the simulation stepping logic shared by main.c's render
 * loops and checks.c's scripted assertions (T07/T08): beacon replay
 * (with the simplified single-hop relay model) and rr_scene_t building.
 *
 * See main.c's file comment for the beacon/relay model and the "full
 * replay from t=0" rationale.
 */
#ifndef SIM_CORE_H
#define SIM_CORE_H

#include "neighbor_table.h"
#include "radar_scene.h"
#include "scenario.h"

#include <stdint.h>

/** course_deg -> cdeg, CL_COURSE_INVALID below 1.5 m/s (docs/05). */
uint16_t course_to_cdeg(float speed_mps, float course_deg);

/**
 * Rebuilds every unit's neighbour table from scratch by replaying every
 * beacon event from t=0 up to sim_time_ms, with the same seed producing
 * the same delivered/dropped beacons every time (deterministic PRNG).
 */
void replay_to(const scenario_t *scn, uint32_t sim_time_ms, uint32_t seed,
               float range_m, float loss_p, nt_t nt_out[SCN_MAX_UNITS]);

/**
 * Builds the rr_scene_t for `pov` at sim_time_ms from already-replayed
 * neighbour tables (index [pov] is read). zoom_mode RR_ZOOM_AUTO resolves
 * via rr_pick_zoom over the current LIVE/STALE neighbours.
 */
void build_scene(const scenario_t *scn, nt_t nt[SCN_MAX_UNITS], uint8_t pov,
                 uint32_t sim_time_ms, rr_zoom_t zoom_mode, rr_scene_t *sc);

/** RGB565 -> RGB888 (row-major, 3 bytes/pixel), n = pixel count. */
void rgb565_to_rgb888(const uint16_t *px, uint8_t *out, int n);

#endif /* SIM_CORE_H */
