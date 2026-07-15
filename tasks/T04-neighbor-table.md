# T04 — `neighbor_table`: convoy state component

**Depends:** none (uses convoy_proto, already present) · **Phase:** M1 (pure C)
**Required reading:** `docs/05-gps-geo.md` §Neighbour table + §Staleness;
`docs/03-radio-protocol.md` §Beacon relay

## Goal

The single owner of "who is out there": beacon ingestion with seq ordering,
staleness tiers, and relay bookkeeping (decide/mark/suppress).

## Deliverables

- `firmware/components/neighbor_table/include/neighbor_table.h`
- `firmware/components/neighbor_table/neighbor_table.c`
- `firmware/components/neighbor_table/CMakeLists.txt`
- `test/host/test_neighbor_table.c`

## Interface contract

```c
#include "convoy_proto.h"

typedef enum { NT_LIVE, NT_STALE, NT_GHOST, NT_GONE } nt_tier_t;
typedef enum { NT_RES_NEW, NT_RES_UPDATED, NT_RES_OLD_IGNORED, NT_RES_SELF_IGNORED } nt_result_t;

typedef struct {
    bool     in_use;
    uint8_t  uid;
    char     initials[2];
    cl_beacon_t last;          /* full latest beacon (newest seq)        */
    uint16_t last_seq;
    uint32_t last_heard_ms;    /* local clock at reception               */
    bool     via_relay;        /* newest copy arrived with hop == 1      */
    uint16_t last_relayed_seq; /* relay bookkeeping                      */
    bool     relayed_any;      /* false until first relay mark           */
} nt_entry_t;

typedef struct { uint8_t self_uid; nt_entry_t e[CL_MAX_UNITS]; } nt_t;

void        nt_init(nt_t *t, uint8_t self_uid);
nt_result_t nt_update_from_beacon(nt_t *t, const cl_beacon_t *b, uint32_t now_ms);
/* True at most once per (uid, seq), only for hop==0, never for self/old seq.
 * Marks the seq as relayed when returning true. */
bool        nt_should_relay(nt_t *t, const cl_beacon_t *b);
/* Heard someone else's hop-1 copy: suppress our pending relay of (uid,seq).
 * (Also called implicitly by nt_update_from_beacon for hop==1 packets.) */
void        nt_note_relayed(nt_t *t, uint8_t uid, uint16_t seq);
nt_tier_t   nt_tier(const nt_entry_t *e, uint32_t now_ms);
/* Copies entries with tier != NT_GONE into out, insertion order.
 * Returns count. */
int         nt_snapshot(const nt_t *t, nt_entry_t *out, uint32_t now_ms);
```

Binding rules: updates apply only when `cl_seq_newer(b->seq, last_seq)`
(first beacon from a uid always applies); beacons from `self_uid` →
`NT_RES_SELF_IGNORED`; all age math wrap-safe `(int32_t)(now - then)`;
tier thresholds from `convoy_cfg.h` (`< NT_STALE_MS` → LIVE, `< NT_GHOST_MS`
→ STALE, `< NT_GONE_MS` → GHOST, else GONE).

## Test requirements

1. New beacon → NEW; newer seq → UPDATED (fields refreshed); equal/older
   seq → OLD_IGNORED (state untouched); self → SELF_IGNORED.
2. Relayed (hop 1) copy arriving **before** the direct copy: applies with
   `via_relay=true`; direct copy with same seq → OLD_IGNORED but
   `via_relay` stays true (document: equal seq never re-applies).
3. Tier boundaries exact: ages `NT_STALE_MS−1 / NT_STALE_MS /
   NT_GHOST_MS / NT_GONE_MS` → LIVE / STALE / GHOST / GONE.
4. Clock wrap: `last_heard_ms = 0xFFFFF000`, `now = 0x00000800` → age
   computed correctly (~6 s → LIVE).
5. Relay logic: hop-0 beacon → `nt_should_relay` true exactly once; same
   seq again → false; hop-1 → always false; `nt_note_relayed` first →
   false; seq wrap across 65535→0 still relays new seqs.
6. Snapshot excludes GONE, preserves order, count correct with gaps
   (units 0, 3 only).
7. Beacon with `fix_quality == 0` still tracked (tier from arrival time).

## Acceptance — CI

`make -C test/host test` green.

## Out of scope

Actual relay TX scheduling/timers (T16), distance sorting (UI does it, T06).
