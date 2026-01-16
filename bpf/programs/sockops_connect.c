#include <linux/bpf.h>
#include <linux/in.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include "../include/nity_common.h"
#include "../include/nity_maps.h"

static __always_inline struct nity_counters* nity_get_counters(void)
{
    __u32 k = 0;
    return bpf_map_lookup_elem(&counters, &k);
}

static __always_inline __u64 nity_get_epoch(void)
{
    __u32 k = 0;
    __u64 *e = bpf_map_lookup_elem(&epoch, &k);
    return e ? *e : 0;
}

static __always_inline __u32 nity_get_active_table(void)
{
    __u32 k = 0;
    __u32 *t = bpf_map_lookup_elem(&active_table, &k);
    return t ? *t : NITY_ACTIVE_TABLE_A;
}

static __always_inline __u32 nity_get_admission_mode(void)
{
    __u32 k = 0;
    __u32 *m = bpf_map_lookup_elem(&admission_mode, &k);
    return m ? *m : NITY_ADMISSION_NORMAL;
}

/* deterministic per-route gear: counter++ % total_slots */
static __always_inline __u32 nity_next_slot(const struct nity_route_id *rid,
                                            __u32 total_slots,
                                            struct nity_counters *c)
{
    __u32 *ctr = bpf_map_lookup_elem(&route_counter, rid);
    if (!ctr) {
        if (c) c->route_counter_miss++;
        __u32 zero = 0;
        /* Best-effort create; ignore failure */
        bpf_map_update_elem(&route_counter, rid, &zero, BPF_NOEXIST);
        ctr = bpf_map_lookup_elem(&route_counter, rid);
        if (!ctr) {
            /* fail-open: pick slot 0 deterministically */
            return 0;
        }
    }

    /* Atomic fetch-add for cross-CPU determinism */
    __u32 v = __sync_fetch_and_add(ctr, 1);
    if (total_slots == 0) return 0;
    return v % total_slots;
}

static __always_inline int nity_pick_backend(struct bpf_sock_addr *ctx,
                                             struct nity_backend *out,
                                             struct nity_counters *c)
{
    const __u64 cur_epoch = nity_get_epoch();

    /* route_id based on ORIGINAL destination (service VIP / port / proto) */
    struct nity_route_id rid = {
        .vip   = ctx->user_ip4,           /* keep kernel order */
        .vport = (__u16)ctx->user_port,   /* keep kernel order */
        .proto = (__u8)(ctx->protocol ? ctx->protocol : IPPROTO_TCP),
        ._pad  = 0,
    };

    /* flow key: socket cookie + route_id */
    struct nity_flow_key fk = {0};
    fk.rid = rid;
    fk._pad = 0;

    if (ctx->sk) {
        fk.cookie = bpf_get_socket_cookie(ctx->sk);
    } else {
        /* fallback: pid_tgid as a weak proxy (still deterministic) */
        fk.cookie = bpf_get_current_pid_tgid();
    }

    /* 1) Conntrack stickiness */
    struct nity_flow_val *fv = bpf_map_lookup_elem(&conntrack_LRU, &fk);
    if (fv && fv->epoch == cur_epoch) {
        if (c) c->conntrack_hits++;
        *out = fv->be;
        return 1;
    }
    if (c) c->conntrack_misses++;

    /* 2) route_cfg lookup: total_slots */
    struct nity_route_cfg *cfg = bpf_map_lookup_elem(&route_cfg, &rid);
    if (!cfg || cfg->total_slots == 0) {
        if (c) c->route_cfg_miss++;
        return 0; /* fail-open: no redirect */
    }

    /* 3) deterministic gear slot */
    __u32 slot = nity_next_slot(&rid, cfg->total_slots, c);

    /* 4) choose active slot table and lookup backend */
    struct nity_route_key rk = {
        .vip   = rid.vip,
        .vport = rid.vport,
        .proto = rid.proto,
        ._pad  = 0,
        .slot  = slot,
    };

    if (c) c->slot_lookups++;

    struct nity_backend *be = NULL;
    __u32 tbl = nity_get_active_table();
    if (tbl == NITY_ACTIVE_TABLE_B) {
        be = bpf_map_lookup_elem(&slot_table_B, &rk);
    } else {
        be = bpf_map_lookup_elem(&slot_table_A, &rk);
    }

    if (!be) {
        if (c) c->slot_lookup_fails++;
        return 0; /* fail-open: no redirect */
    }

    *out = *be;

    /* 5) update conntrack with current epoch */
    struct nity_flow_val newv = {
        .be = *be,
        .ts_ns = bpf_ktime_get_ns(),
        .epoch = cur_epoch,
    };
    bpf_map_update_elem(&conntrack_LRU, &fk, &newv, BPF_ANY);
    if (c) c->conntrack_updates++;

    return 1;
}

/*
 * Hook: CGROUP_INET4_CONNECT
 * - If admission_mode == HARD: deny new connects (circuit breaker).
 * - Otherwise: attempt deterministic redirect to backend (fail-open if missing config).
 *
 * Return convention for CGROUP_SOCK_ADDR family:
 * - 1 => allow
 * - 0 => deny
 */
SEC("cgroup/connect4")
int nity_connect4(struct bpf_sock_addr *ctx)
{
    struct nity_counters *c = nity_get_counters();
    if (c) c->connect_events++;

    __u32 mode = nity_get_admission_mode();
    if (mode == NITY_ADMISSION_HARD) {
        if (c) c->admission_hard_denies++;
        return 0; /* deny */
    }

    struct nity_backend be = {0};
    int ok = nity_pick_backend(ctx, &be, c);
    if (!ok) {
        return 1; /* allow original destination (fail-open) */
    }

    /* Redirect: overwrite destination (still O(1)) */
    ctx->user_ip4  = be.ip4;
    ctx->user_port = be.port;

    if (c) c->redirects++;
    return 1; /* allow */
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
