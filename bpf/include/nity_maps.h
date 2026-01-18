// SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause)
#pragma once

#include <linux/bpf.h>
#include <linux/types.h>

#include "nity_common.h"

// Minimal libbpf-style map definition macros (avoid external bpf_helpers.h).
#ifndef SEC
#define SEC(NAME) __attribute__((section(NAME), used))
#endif

#ifndef __uint
#define __uint(name, val) int (*name)[val]
#endif
#ifndef __type
#define __type(name, val) val *name
#endif

// libbpf pinning constants (kept local to avoid external headers).
#ifndef LIBBPF_PIN_NONE
#define LIBBPF_PIN_NONE 0
#endif
#ifndef LIBBPF_PIN_BY_NAME
#define LIBBPF_PIN_BY_NAME 2
#endif

// ----------------------------
// Size knobs (bounded maps)
// ----------------------------
#define NITY_MAX_SLOT_ENTRIES      65536u
#define NITY_MAX_ROUTE_GROUPS      4096u
#define NITY_MAX_FALLBACK_BACKENDS 16384u
#define NITY_MAX_CONNTRACK_ENTRIES 65536u

// ----------------------------
// Required maps (RFC0004)
// ----------------------------

// 6.1 Slot tables (A/B)
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, NITY_MAX_SLOT_ENTRIES);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
    __type(key, __u64); // route_key
    __type(value, struct nity_backend_id);
} slot_table_A SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, NITY_MAX_SLOT_ENTRIES);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
    __type(key, __u64); // route_key
    __type(value, struct nity_backend_id);
} slot_table_B SEC(".maps");

// 6.2 Active table selector + epoch
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
    __type(key, __u32);
    __type(value, __u32); // enum nity_active_table
} active_table SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
    __type(key, __u32);
    __type(value, __u64);
} epoch SEC(".maps");

// 6.3 Conntrack LRU
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, NITY_MAX_CONNTRACK_ENTRIES);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
    __type(key, __u64); // flow_key
    __type(value, struct nity_conntrack_val);
} conntrack_lru SEC(".maps");

// 6.4 Agent heartbeat
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
    __type(key, __u32);
    __type(value, __u64); // ns timestamp
} last_agent_seen_ts SEC(".maps");

// 6.5 Control map (per route group)
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, NITY_MAX_ROUTE_GROUPS);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
    __type(key, __u64); // route_group_key
    __type(value, struct nity_rt_control);
} rt_control SEC(".maps");

// 7. Option A fallback backend sets
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, NITY_MAX_ROUTE_GROUPS);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
    __type(key, __u64); // route_group_key
    __type(value, __u32); // N
} fallback_size SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, NITY_MAX_FALLBACK_BACKENDS);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
    __type(key, struct nity_fallback_key);
    __type(value, struct nity_backend_id);
} fallback_backends SEC(".maps");

// 6.6 Counters (per-CPU where possible)
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, NITY_C_MAX);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
    __type(key, __u32);
    __type(value, __u64);
} counters SEC(".maps");