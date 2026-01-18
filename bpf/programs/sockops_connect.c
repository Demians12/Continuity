// SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause)

#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/types.h>

#include "nity_maps.h"

#ifndef SEC
#define SEC(NAME) __attribute__((section(NAME), used))
#endif

// ----------------------------
// Minimal helper definitions (no external bpf_helpers.h dependency)
// ----------------------------

static void *(*bpf_map_lookup_elem)(void *map, const void *key) =
    (void *)BPF_FUNC_map_lookup_elem;

static long (*bpf_map_update_elem)(void *map, const void *key, const void *value, __u64 flags) =
    (void *)BPF_FUNC_map_update_elem;

static __u64 (*bpf_ktime_get_ns)(void) =
    (void *)BPF_FUNC_ktime_get_ns;

static __always_inline void nity_inc_counter(__u32 id)
{
    __u32 k = id;
    __u64 *v = bpf_map_lookup_elem(&counters, &k);
    if (v) {
        *v += 1;
    }
}

static __always_inline enum nity_failsafe_mode nity_get_failsafe_mode(__u64 now_ns)
{
    __u32 k0 = 0;
    __u64 *lastp = bpf_map_lookup_elem(&last_agent_seen_ts, &k0);
    __u64 last = lastp ? *lastp : 0;

    // last == 0 means "unknown"; treat as stale.
    __u64 age = (last == 0) ? ~0ULL : (now_ns - last);

    if (age >= NITY_FAILSAFE_T2_NS) {
        return NITY_FAILSAFE_FALLBACK;
    }
    if (age >= NITY_FAILSAFE_T1_NS) {
        return NITY_FAILSAFE_HOLD;
    }
    return NITY_FAILSAFE_NORMAL;
}

// Bounded double-read to avoid recording a mismatched (active_table, epoch) pair across a flip.
static __always_inline void nity_read_epoch_and_active_table(__u64 *epoch_out, __u32 *table_out)
{
    __u32 k0 = 0;

    __u64 e1 = 0;
    __u64 *e1p = bpf_map_lookup_elem(&epoch, &k0);
    if (e1p) {
        e1 = *e1p;
    }

    __u32 t = NITY_TABLE_A;
    __u32 *tp = bpf_map_lookup_elem(&active_table, &k0);
    if (tp) {
        t = *tp;
    }

    __u64 e2 = e1;
    __u64 *e2p = bpf_map_lookup_elem(&epoch, &k0);
    if (e2p) {
        e2 = *e2p;
    }

    if (e2 != e1) {
        tp = bpf_map_lookup_elem(&active_table, &k0);
        if (tp) {
            t = *tp;
        }
        e2p = bpf_map_lookup_elem(&epoch, &k0);
        if (e2p) {
            e2 = *e2p;
        }
    }

    *epoch_out = e2;
    *table_out = t;
}

static __always_inline int nity_select_fallback(__u64 route_group_key,
                                               __u64 flow_key,
                                               struct nity_backend_id *out)
{
    __u32 *np = bpf_map_lookup_elem(&fallback_size, &route_group_key);
    if (!np) {
        return -1;
    }

    __u32 n = *np;
    if (n == 0) {
        return -1;
    }

    // Deterministic pick within the route group. (RFC0003/0004)
    __u32 idx = (__u32)(nity_mix64(flow_key) % (__u64)n);
    struct nity_fallback_key fk = {
        .route_group_key = route_group_key,
        .idx = idx,
        ._pad = 0,
    };

    struct nity_backend_id *bp = bpf_map_lookup_elem(&fallback_backends, &fk);
    if (!bp) {
        return -1;
    }

    *out = *bp;
    return 0;
}

static __always_inline int nity_select_slot(__u64 route_key, __u32 active, struct nity_backend_id *out)
{
    struct nity_backend_id *bp = 0;

    if (active == NITY_TABLE_B) {
        bp = bpf_map_lookup_elem(&slot_table_B, &route_key);
    } else {
        bp = bpf_map_lookup_elem(&slot_table_A, &route_key);
    }

    if (!bp) {
        return -1;
    }

    *out = *bp;
    return 0;
}

// Connect-centric MVP hook.
// Attach type: BPF_CGROUP_INET4_CONNECT ("cgroup/connect4")
// - O(1) map lookups, no backend loops.
// - deterministic selection (flow_key -> slot -> route_key).
// - conntrack LRU (stickiness).
// - failsafe mode derived from last_agent_seen_ts.
SEC("cgroup/connect4")
int nity_connect4(struct bpf_sock_addr *ctx)
{
    nity_inc_counter(NITY_C_REQS_TOTAL);

    // Destination (VIP) from connect() args.
    __u32 vip_be = ctx->user_ip4;
    __u16 vport_be = (__u16)ctx->user_port;
    __u8 proto = (__u8)ctx->protocol;

    __u64 now_ns = bpf_ktime_get_ns();
    enum nity_failsafe_mode fmode = nity_get_failsafe_mode(now_ns);

    __u64 epoch_now = 0;
    __u32 active = NITY_TABLE_A;
    nity_read_epoch_and_active_table(&epoch_now, &active);

    __u64 route_group_key = nity_hash_route_group(vip_be, vport_be, proto);

    // Admission mode (RFC0004 section 5/6.5).
    struct nity_rt_control *ctl = bpf_map_lookup_elem(&rt_control, &route_group_key);
    if (ctl) {
        __u32 major = ctl->schema_version >> 16;
        if (major != NITY_SCHEMA_MAJOR) {
            nity_inc_counter(NITY_C_SCHEMA_MISMATCH);
            // Do NOT hard-fail traffic on schema mismatch in dataplane;
            // agent should refuse to run per RFC, but dataplane stays safe.
        }

        if (ctl->admission_mode == NITY_ADMISSION_HARD) {
            nity_inc_counter(NITY_C_DENY_TOTAL);
            return 0; // deny connect
        }
    }

    // Best-effort source identity.
    // For this hook, src_port can be 0 until the kernel assigns the ephemeral port.
    // The reduction policy is documented in RFC0004 section 4.2.
    __u32 src_ip_be = 0;
    __u32 src_port_host = 0;
    if (ctx->sk) {
        src_ip_be = ctx->sk->src_ip4;   // network order
        src_port_host = ctx->sk->src_port; // host order; may be 0 at this stage
    }
    if (src_ip_be == 0) {
        // msg_src_ip4 is best-effort and may also be 0.
        src_ip_be = ctx->msg_src_ip4;
    }

    __u64 flow_key = nity_hash_flow_key(src_ip_be, src_port_host, vip_be, vport_be, proto);

    // Conntrack LRU (RFC0004 section 6.3).
    struct nity_conntrack_val *ct = bpf_map_lookup_elem(&conntrack_lru, &flow_key);
    struct nity_backend_id chosen = {0};
    int have_backend = 0;

    if (ct) {
        nity_inc_counter(NITY_C_CONNTRACK_HIT);
        chosen = ct->backend;
        have_backend = 1;

        // Refresh last_seen/epoch_seen without changing backend.
        ct->last_seen_ns = now_ns;
        ct->epoch_seen = epoch_now;
    } else {
        nity_inc_counter(NITY_C_CONNTRACK_MISS);

        // Deterministic slot selection.
        __u32 slot = (__u32)(nity_mix64(flow_key) & NITY_SLOTS_MASK);
        __u64 route_key = nity_hash_route_key(vip_be, vport_be, proto, slot);

        if (fmode == NITY_FAILSAFE_FALLBACK) {
            if (nity_select_fallback(route_group_key, flow_key, &chosen) == 0) {
                have_backend = 1;
                nity_inc_counter(NITY_C_FALLBACK_USED);
            } else {
                nity_inc_counter(NITY_C_MAP_LOOKUP_FAIL);
            }
        } else {
            // NORMAL + HOLD: use active slot table.
            // HOLD flip refusal is handled by the agent in MVP; dataplane still derives HOLD.
            if (nity_select_slot(route_key, active, &chosen) == 0) {
                have_backend = 1;
            } else {
                // Slot missing => fallback within same route group.
                if (nity_select_fallback(route_group_key, flow_key, &chosen) == 0) {
                    have_backend = 1;
                    nity_inc_counter(NITY_C_FALLBACK_USED);
                } else {
                    nity_inc_counter(NITY_C_MAP_LOOKUP_FAIL);
                }
            }
        }

        if (have_backend) {
            struct nity_conntrack_val val = {
                .backend = chosen,
                .last_seen_ns = now_ns,
                .epoch_seen = epoch_now,
            };
            (void)bpf_map_update_elem(&conntrack_lru, &flow_key, &val, BPF_ANY);
        }
    }

    // If a backend was selected, rewrite connect() destination to it.
    if (have_backend && chosen.ip4 != 0 && chosen.port_be != 0) {
        ctx->user_ip4 = chosen.ip4;
        ctx->user_port = (__u32)chosen.port_be;
        nity_inc_counter(NITY_C_REWRITE_TOTAL);
    }

    // Allow connect.
    return 1;
}

char LICENSE[] SEC("license") = "GPL";
