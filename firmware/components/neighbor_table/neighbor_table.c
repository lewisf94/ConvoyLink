/* neighbor_table.c — see include/neighbor_table.h, docs/05, docs/03 */
#include "neighbor_table.h"

#include <string.h>

void nt_init(nt_t *t, uint8_t self_uid)
{
    memset(t, 0, sizeof(*t));
    t->self_uid = self_uid;
}

nt_result_t nt_update_from_beacon(nt_t *t, const cl_beacon_t *b,
                                  uint32_t now_ms)
{
    if (b->hdr.sender == t->self_uid) {
        return NT_RES_SELF_IGNORED;
    }
    if (b->hdr.sender >= CL_MAX_UNITS) {
        return NT_RES_OLD_IGNORED; /* defensive: cl_validate guarantees this */
    }

    bool is_relay = (b->hdr.meta != 0);
    if (is_relay) {
        nt_note_relayed(t, b->hdr.sender, b->seq);
    }

    nt_entry_t *e = &t->e[b->hdr.sender];
    bool first = !e->in_use;
    if (!first && !cl_seq_newer(b->seq, e->last_seq)) {
        return NT_RES_OLD_IGNORED;
    }

    e->in_use = true;
    e->uid = b->hdr.sender;
    e->initials[0] = b->initials[0];
    e->initials[1] = b->initials[1];
    e->last = *b;
    e->last_seq = b->seq;
    e->last_heard_ms = now_ms;
    e->via_relay = is_relay;

    return first ? NT_RES_NEW : NT_RES_UPDATED;
}

bool nt_should_relay(nt_t *t, const cl_beacon_t *b)
{
    if (b->hdr.sender == t->self_uid || b->hdr.sender >= CL_MAX_UNITS) {
        return false;
    }
    if (b->hdr.meta != 0) {
        return false; /* only hop==0 originals are relay candidates */
    }

    nt_entry_t *e = &t->e[b->hdr.sender];
    if (e->relayed_any && !cl_seq_newer(b->seq, e->last_relayed_seq)) {
        return false; /* already relayed this seq (or a newer one) */
    }

    e->last_relayed_seq = b->seq;
    e->relayed_any = true;
    return true;
}

void nt_note_relayed(nt_t *t, uint8_t uid, uint16_t seq)
{
    if (uid >= CL_MAX_UNITS) {
        return;
    }
    nt_entry_t *e = &t->e[uid];
    if (!e->relayed_any || cl_seq_newer(seq, e->last_relayed_seq)) {
        e->last_relayed_seq = seq;
        e->relayed_any = true;
    }
}

nt_tier_t nt_tier(const nt_entry_t *e, uint32_t now_ms)
{
    uint32_t age = (uint32_t)(int32_t)(now_ms - e->last_heard_ms);
    if (age < NT_STALE_MS) {
        return NT_LIVE;
    }
    if (age < NT_GHOST_MS) {
        return NT_STALE;
    }
    if (age < NT_GONE_MS) {
        return NT_GHOST;
    }
    return NT_GONE;
}

int nt_snapshot(const nt_t *t, nt_entry_t *out, uint32_t now_ms)
{
    int count = 0;
    for (int i = 0; i < CL_MAX_UNITS; i++) {
        const nt_entry_t *e = &t->e[i];
        if (!e->in_use) {
            continue;
        }
        if (nt_tier(e, now_ms) == NT_GONE) {
            continue;
        }
        out[count++] = *e;
    }
    return count;
}
