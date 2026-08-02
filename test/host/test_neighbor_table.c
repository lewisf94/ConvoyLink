/* Host tests for neighbor_table (docs/05, docs/03). */
#include "neighbor_table.h"
#include "tinytest.h"

#include <string.h>

#define SELF 4

static cl_beacon_t mk(uint8_t sender, uint16_t seq, const char *initials)
{
    cl_beacon_t b;
    cl_make_beacon(&b, sender, seq, initials, 500000000, 0, 0,
                   CL_COURSE_INVALID, 3, 5, 0);
    return b;
}

TT_TEST(new_updated_old_self)
{
    nt_t t;
    nt_init(&t, SELF);

    cl_beacon_t b1 = mk(0, 10, "AA");
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &b1, 1000), NT_RES_NEW);
    TT_ASSERT(t.e[0].in_use);
    TT_ASSERT_EQ(t.e[0].last_seq, 10);
    TT_ASSERT_EQ(t.e[0].last_heard_ms, 1000);

    cl_beacon_t b2 = mk(0, 11, "AA");
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &b2, 2000), NT_RES_UPDATED);
    TT_ASSERT_EQ(t.e[0].last_seq, 11);
    TT_ASSERT_EQ(t.e[0].last_heard_ms, 2000);

    /* equal seq: ignored, state untouched */
    cl_beacon_t b3 = mk(0, 11, "AA");
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &b3, 3000), NT_RES_OLD_IGNORED);
    TT_ASSERT_EQ(t.e[0].last_heard_ms, 2000);

    /* older seq: ignored, state untouched */
    cl_beacon_t b4 = mk(0, 5, "AA");
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &b4, 4000), NT_RES_OLD_IGNORED);
    TT_ASSERT_EQ(t.e[0].last_seq, 11);
    TT_ASSERT_EQ(t.e[0].last_heard_ms, 2000);

    /* self: always ignored, no entry created */
    cl_beacon_t bself = mk(SELF, 1, "ME");
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &bself, 5000), NT_RES_SELF_IGNORED);
    TT_ASSERT(!t.e[SELF].in_use);
}

TT_TEST(relay_before_original_preserves_via_relay)
{
    nt_t t;
    nt_init(&t, SELF);

    cl_beacon_t relay = mk(1, 20, "BB");
    cl_beacon_to_relay(&relay); /* hop = 1 */
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &relay, 1000), NT_RES_NEW);
    TT_ASSERT(t.e[1].via_relay);

    cl_beacon_t direct = mk(1, 20, "BB"); /* same seq, hop = 0 */
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &direct, 1500), NT_RES_OLD_IGNORED);
    TT_ASSERT(t.e[1].via_relay);             /* stays true                */
    TT_ASSERT_EQ(t.e[1].last_heard_ms, 1000); /* truly untouched          */
}

TT_TEST(tier_boundaries_exact)
{
    nt_entry_t e;
    memset(&e, 0, sizeof(e));
    e.last_heard_ms = 0;

    TT_ASSERT_EQ(nt_tier(&e, NT_STALE_MS - 1), NT_LIVE);
    TT_ASSERT_EQ(nt_tier(&e, NT_STALE_MS), NT_STALE);
    TT_ASSERT_EQ(nt_tier(&e, NT_GHOST_MS), NT_GHOST);
    TT_ASSERT_EQ(nt_tier(&e, NT_GONE_MS), NT_GONE);
}

TT_TEST(tier_clock_wrap_safe)
{
    nt_entry_t e;
    memset(&e, 0, sizeof(e));
    e.last_heard_ms = 0xFFFFF000u;

    TT_ASSERT_EQ(nt_tier(&e, 0x00000800u), NT_LIVE); /* ~6.1s, wrap-safe */
}

TT_TEST(relay_scheduling_logic)
{
    nt_t t;
    nt_init(&t, SELF);

    cl_beacon_t hop0 = mk(2, 100, "CC");
    TT_ASSERT(nt_should_relay(&t, &hop0));
    TT_ASSERT(!nt_should_relay(&t, &hop0)); /* same seq again -> false */

    cl_beacon_t hop1 = mk(2, 101, "CC");
    cl_beacon_to_relay(&hop1);
    TT_ASSERT(!nt_should_relay(&t, &hop1)); /* hop==1 -> always false */

    /* note_relayed called first (as if hearing someone else's relay) */
    nt_note_relayed(&t, 2, 200);
    cl_beacon_t hop0_200 = mk(2, 200, "CC");
    TT_ASSERT(!nt_should_relay(&t, &hop0_200));

    /* self is never a relay candidate */
    cl_beacon_t selfb = mk(SELF, 1, "ME");
    TT_ASSERT(!nt_should_relay(&t, &selfb));

    /* seq wrap 65535 -> 0 still relays new seqs */
    cl_beacon_t wrap_hi = mk(3, 65535, "DD");
    TT_ASSERT(nt_should_relay(&t, &wrap_hi));
    cl_beacon_t wrap_lo = mk(3, 0, "DD");
    TT_ASSERT(nt_should_relay(&t, &wrap_lo));
}

TT_TEST(snapshot_excludes_gone_preserves_order_with_gaps)
{
    nt_t t;
    nt_init(&t, SELF);

    cl_beacon_t b0 = mk(0, 1, "AA");
    cl_beacon_t b3 = mk(3, 1, "DD");
    nt_update_from_beacon(&t, &b0, 1000);
    nt_update_from_beacon(&t, &b3, 1000);

    nt_entry_t out[CL_MAX_UNITS];
    int n = nt_snapshot(&t, out, 2000);
    TT_ASSERT_EQ(n, 2);
    TT_ASSERT_EQ(out[0].uid, 0);
    TT_ASSERT_EQ(out[1].uid, 3);

    /* age uid0/uid3 into GONE together; both drop out */
    n = nt_snapshot(&t, out, 1000 + NT_GONE_MS);
    TT_ASSERT_EQ(n, 0);

    /* refresh uid3 only, leaving uid0 to go GONE alone */
    cl_beacon_t b3_again = mk(3, 2, "DD");
    nt_update_from_beacon(&t, &b3_again, 1000 + NT_GONE_MS);
    n = nt_snapshot(&t, out, 1000 + NT_GONE_MS);
    TT_ASSERT_EQ(n, 1);
    TT_ASSERT_EQ(out[0].uid, 3);
}

TT_TEST(zero_fix_quality_still_tracked)
{
    nt_t t;
    nt_init(&t, SELF);

    cl_beacon_t b;
    cl_make_beacon(&b, 1, 1, "BB", 0, 0, 0, CL_COURSE_INVALID, /*fix_quality=*/0,
                   /*sats=*/0, 0);
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &b, 1000), NT_RES_NEW);
    TT_ASSERT(t.e[1].in_use);
    TT_ASSERT_EQ(t.e[1].last.fix_quality, 0);
    TT_ASSERT_EQ(nt_tier(&t.e[1], 1000), NT_LIVE);
}


/* --- accept-after-silence (docs/05 §Sequence resync) ------------------- */

/* The exact failure this rule exists to fix: a unit reboots after a long
 * run, its seq restarts at 0, and every later beacon reads as "older". */
TT_TEST(reboot_resync_after_silence)
{
    nt_t t;
    nt_init(&t, SELF);

    /* U0 runs a long while, reaching seq 500. */
    for (uint16_t s = 0; s <= 500; s++) {
        cl_beacon_t b = mk(0, s, "AA");
        nt_update_from_beacon(&t, &b, 1000u * s);
    }
    TT_ASSERT_EQ(t.e[0].last_seq, 500);
    uint32_t t_reboot = 1000u * 500u;

    /* It power-cycles and comes back 4 s later with seq 0. Too soon to
     * resync, so it is still (correctly) rejected as out of order. */
    cl_beacon_t fresh = mk(0, 0, "AA");
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &fresh, t_reboot + 4000),
                 NT_RES_OLD_IGNORED);
    TT_ASSERT_EQ(t.e[0].last_seq, 500);

    /* Because we keep rejecting, last_heard never refreshes and the
     * silence grows on its own — so the resync always eventually fires. */
    cl_beacon_t again = mk(0, 3, "AA");
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &again, t_reboot + NT_RESYNC_MS),
                 NT_RES_UPDATED);
    TT_ASSERT_EQ(t.e[0].last_seq, 3);
    TT_ASSERT_EQ(t.e[0].last_heard_ms, t_reboot + NT_RESYNC_MS);

    /* And it tracks normally from there. */
    cl_beacon_t next = mk(0, 4, "AA");
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &next,
                                       t_reboot + NT_RESYNC_MS + 5000),
                 NT_RES_UPDATED);
    TT_ASSERT_EQ(t.e[0].last_seq, 4);
}

/* The exception must not weaken ordering while the neighbour is live —
 * otherwise a delayed relay copy could rewind a good position. */
TT_TEST(no_resync_while_still_live)
{
    nt_t t;
    nt_init(&t, SELF);

    cl_beacon_t b = mk(1, 100, "BB");
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &b, 10000), NT_RES_NEW);

    /* Just inside the window: still ordered by seq. */
    cl_beacon_t old = mk(1, 42, "BB");
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &old, 10000 + NT_RESYNC_MS - 1),
                 NT_RES_OLD_IGNORED);
    TT_ASSERT_EQ(t.e[1].last_seq, 100);

    /* Exactly at the boundary the exception applies. */
    cl_beacon_t rejoin = mk(1, 42, "BB");
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &rejoin, 10000 + NT_RESYNC_MS),
                 NT_RES_UPDATED);
    TT_ASSERT_EQ(t.e[1].last_seq, 42);
}

/* Relay bookkeeping is seq-based too, so it has to restart with the
 * sender — otherwise we would track a rebooted unit but never relay for
 * it again, silently halving its range. */
TT_TEST(resync_restarts_relay_bookkeeping)
{
    nt_t t;
    nt_init(&t, SELF);

    cl_beacon_t high = mk(2, 900, "CC");
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &high, 1000), NT_RES_NEW);
    TT_ASSERT(nt_should_relay(&t, &high));      /* relayed once */
    TT_ASSERT(!nt_should_relay(&t, &high));     /* not twice */

    /* Without the reset, seq 1 would look older than last_relayed_seq
     * (900) forever and never be relayed again. */
    cl_beacon_t rebooted = mk(2, 1, "CC");
    TT_ASSERT_EQ(nt_update_from_beacon(&t, &rebooted, 1000 + NT_RESYNC_MS),
                 NT_RES_UPDATED);
    TT_ASSERT(nt_should_relay(&t, &rebooted));
}

int main(void)
{
    TT_RUN(new_updated_old_self);
    TT_RUN(relay_before_original_preserves_via_relay);
    TT_RUN(tier_boundaries_exact);
    TT_RUN(tier_clock_wrap_safe);
    TT_RUN(relay_scheduling_logic);
    TT_RUN(snapshot_excludes_gone_preserves_order_with_gaps);
    TT_RUN(zero_fix_quality_still_tracked);
    TT_RUN(reboot_resync_after_silence);
    TT_RUN(no_resync_while_still_live);
    TT_RUN(resync_restarts_relay_bookkeeping);
    return tt_summary("neighbor_table");
}
