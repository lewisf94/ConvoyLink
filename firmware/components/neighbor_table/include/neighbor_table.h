/**
 * neighbor_table — the single owner of "who is out there": beacon
 * ingestion with wrap-safe sequence ordering, staleness tiers, and relay
 * bookkeeping (docs/05-gps-geo.md §Neighbour table + §Staleness,
 * docs/03-radio-protocol.md §Beacon relay).
 *
 * Pure C component: no ESP-IDF headers, no allocation, host-testable.
 * Entries are keyed directly by unit_id (array index == uid).
 */
#ifndef NEIGHBOR_TABLE_H
#define NEIGHBOR_TABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "convoy_proto.h"

typedef enum { NT_LIVE, NT_STALE, NT_GHOST, NT_GONE } nt_tier_t;

typedef enum {
    NT_RES_NEW,
    NT_RES_UPDATED,
    NT_RES_OLD_IGNORED,
    NT_RES_SELF_IGNORED,
} nt_result_t;

typedef struct {
    bool in_use;
    uint8_t uid;
    char initials[2];
    cl_beacon_t last;         /* full latest beacon (newest seq)        */
    uint16_t last_seq;
    uint32_t last_heard_ms;   /* local clock at reception                */
    bool via_relay;           /* newest copy arrived with hop == 1       */
    uint16_t last_relayed_seq; /* relay bookkeeping                      */
    bool relayed_any;         /* false until first relay mark            */
} nt_entry_t;

typedef struct {
    uint8_t self_uid;
    nt_entry_t e[CL_MAX_UNITS];
} nt_t;

/** Zero the table and record which uid is "us" (never tracked/relayed). */
void nt_init(nt_t *t, uint8_t self_uid);

/**
 * Apply a received beacon. Self beacons -> NT_RES_SELF_IGNORED (no
 * effect). A uid's first beacon always applies (NT_RES_NEW) regardless of
 * seq; afterwards only cl_seq_newer(b->seq, entry.last_seq) applies
 * (NT_RES_UPDATED) — an equal/older seq is NT_RES_OLD_IGNORED and leaves
 * every field, including via_relay, untouched. Hop==1 beacons also mark
 * (uid, seq) as relayed (see nt_note_relayed) regardless of apply outcome.
 */
nt_result_t nt_update_from_beacon(nt_t *t, const cl_beacon_t *b,
                                  uint32_t now_ms);

/**
 * True at most once per (uid, seq), only for hop==0 originals, never for
 * self. Marks the seq as relayed when returning true, so a later call for
 * the same or an older seq returns false.
 */
bool nt_should_relay(nt_t *t, const cl_beacon_t *b);

/**
 * Record that (uid, seq) has already been relayed (by us or, on hearing a
 * hop==1 copy, by someone else) — suppresses a pending/future relay of
 * that beacon. Only advances the mark forward (cl_seq_newer).
 */
void nt_note_relayed(nt_t *t, uint8_t uid, uint16_t seq);

/** Staleness tier from age = wrap-safe (now_ms - e->last_heard_ms). */
nt_tier_t nt_tier(const nt_entry_t *e, uint32_t now_ms);

/**
 * Copy every in-use, non-GONE entry into out[] (ascending uid order).
 * out must hold at least CL_MAX_UNITS entries. Returns the count copied.
 */
int nt_snapshot(const nt_t *t, nt_entry_t *out, uint32_t now_ms);

#endif /* NEIGHBOR_TABLE_H */
