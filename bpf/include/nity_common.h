// SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause)
#pragma once

#include <linux/types.h>
#include <linux/in.h>

#ifndef __always_inline
#define __always_inline __attribute__((always_inline)) inline
#endif

// ----------------------------
// Schema / ABI versioning
// ----------------------------
#define NITY_SCHEMA_MAJOR 0u
#define NITY_SCHEMA_MINOR 1u
#define NITY_SCHEMA_VERSION ((NITY_SCHEMA_MAJOR << 16) | NITY_SCHEMA_MINOR)

// ----------------------------
// Deterministic slot field
// ----------------------------
// RFC0004 defines route_key slot range as [0..S_total-1]. MVP uses a fixed,
// power-of-two slot field so modulo is a bitmask (O(1)).
#define NITY_SLOTS_TOTAL 1024u
#define NITY_SLOTS_MASK (NITY_SLOTS_TOTAL - 1u)
#if (NITY_SLOTS_TOTAL & NITY_SLOTS_MASK) != 0
#error "NITY_SLOTS_TOTAL must be power-of-two"
#endif

// ----------------------------
// Failsafe thresholds (ns)
// ----------------------------
// Derived from last_agent_seen_ts age (RFC0003/RFC0004).
// NOTE: These are compile-time defaults for the MVP. They can move into a
// policy field later without changing map keys.
#define NITY_FAILSAFE_T1_NS (2ULL * 1000ULL * 1000ULL * 1000ULL)
#define NITY_FAILSAFE_T2_NS (10ULL * 1000ULL * 1000ULL * 1000ULL)

// ----------------------------
// Enums (stable ABI)
// ----------------------------

enum nity_active_table {
    NITY_TABLE_A = 0,
    NITY_TABLE_B = 1,
};

enum nity_admission_mode {
    NITY_ADMISSION_NORMAL = 0,
    NITY_ADMISSION_SOFT   = 1,
    NITY_ADMISSION_HARD   = 2,
};

enum nity_failsafe_mode {
    NITY_FAILSAFE_NORMAL   = 0,
    NITY_FAILSAFE_HOLD     = 1,
    NITY_FAILSAFE_FALLBACK = 2,
};

enum nity_counter_id {
    NITY_C_REQS_TOTAL = 0,
    NITY_C_DENY_TOTAL,
    NITY_C_REWRITE_TOTAL,
    NITY_C_CONNTRACK_HIT,
    NITY_C_CONNTRACK_MISS,
    NITY_C_FALLBACK_USED,
    NITY_C_MAP_LOOKUP_FAIL,
    NITY_C_SCHEMA_MISMATCH,
    NITY_C_MAX,
};

// ----------------------------
// Shared structs (stable ABI)
// ----------------------------

// Backend identity used by slot tables and fallback backend sets.
// Store in network byte order so dataplane can copy into bpf_sock_addr.
struct nity_backend_id {
    __u32 ip4;      // IPv4 in network byte order
    __u16 port_be;  // L4 port in network byte order
    __u16 _pad;
};

struct nity_conntrack_val {
    struct nity_backend_id backend;
    __u64 last_seen_ns;
    __u64 epoch_seen;
};

// Minimal per-route-group control struct (RFC0004 section 5).
// Keyed by route_group_key = hash(vip, vport, proto).
struct nity_rt_control {
    __u32 schema_version;
    __u8  admission_mode; // enum nity_admission_mode
    __u8  _r0;
    __u16 _r1;

    // Token bucket placeholders (MVP: SOFT enforcement is out of scope here).
    __u64 tokens;
    __u64 refill_rate_per_s;
    __u64 burst;

    // Future-proofing fields.
    __u64 backend_set_hash;
    __u64 policy_flags;
};

// Fallback backend key (Option A, RFC0004 section 7).
struct nity_fallback_key {
    __u64 route_group_key;
    __u32 idx;
    __u32 _pad;
};

// ----------------------------
// Hashing (stable, deterministic)
// ----------------------------
// SplitMix64 is cheap and deterministic.

static __always_inline __u64 nity_mix64(__u64 x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    return x;
}

static __always_inline __u64 nity_hash_combine(__u64 a, __u64 b)
{
    return nity_mix64(a ^ nity_mix64(b));
}

// vip_be: network order, vport_be: network order (lower 16 bits), proto: host order.
static __always_inline __u64 nity_hash_route_group(__u32 vip_be, __u16 vport_be, __u8 proto)
{
    __u64 packed = ((__u64)vip_be << 32) | ((__u64)vport_be << 16) | (__u64)proto;
    return nity_mix64(packed);
}

static __always_inline __u64 nity_hash_route_key(__u32 vip_be, __u16 vport_be, __u8 proto, __u32 slot)
{
    __u64 rg = nity_hash_route_group(vip_be, vport_be, proto);
    return nity_hash_combine(rg, (__u64)slot);
}

// Flow key prefers full 5-tuple.
// MVP documented reduction (RFC0004 section 4.2): when src_port_host == 0 (ephemeral
// port not assigned yet in connect4), treat key as reduced:
// {src_ip, dst_ip, dst_port, proto}. We implement this by hashing with src_port_host == 0.
static __always_inline __u64 nity_hash_flow_key(__u32 src_ip_be,
                                               __u32 src_port_host,
                                               __u32 dst_ip_be,
                                               __u16 dst_port_be,
                                               __u8 proto)
{
    __u64 a = ((__u64)src_ip_be << 32) | (__u64)src_port_host;
    __u64 b = ((__u64)dst_ip_be << 32) | (__u64)dst_port_be;
    return nity_hash_combine(nity_mix64(a), b ^ proto);
}