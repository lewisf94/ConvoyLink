/**
 * checks — scripted --check assertions run against a headless replay of a
 * scenario, so CI catches regressions in the full replay_to/build_scene/
 * rr_screen_draw pipeline, not just unit tests (T08, tasks/T08-sim-scenarios.md).
 */
#ifndef SIM_CHECKS_H
#define SIM_CHECKS_H

#include "scenario.h"

#include <stdint.h>

/**
 * Dispatches by scenario_path's basename to the matching scripted check
 * (split_rejoin.csv, no_fix_start.csv, convoy_cruise.csv). seed/range_m/
 * loss_p are the same replay parameters main.c's headless mode takes.
 * Prints PASS/FAIL lines for each assertion to stdout/stderr.
 * Returns 0 if every assertion for the matched scenario passed, non-zero
 * otherwise (including if the basename matches no known check).
 */
int checks_run(const char *scenario_path, const scenario_t *scn,
               uint32_t seed, float range_m, float loss_p);

#endif /* SIM_CHECKS_H */
