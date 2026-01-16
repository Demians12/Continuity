#ifndef NITY_MAPS_H
#define NITY_MAPS_H

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "nity_common.h"
#include "nity_metrics.h"

/*
 * IMPORTANT:
 * - Keep map sizes conservative for MVP; tune later.
 * - Keys/values MUST match exactly what userspace (agent/loader) expects.
 */

/* Slot tables: route_key -> backend */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 131072);
    __type(key, struct nity_route_key);
    __type(value, struct nity_backend);
} slot_table_A SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 131072);
    __type(key, struct nity_route_key);
    __type(value, struct nity_backend);
} slot_table_B SEC(".maps");

/* Active table selector: index 0 -> u32 (0=A, 1=B) */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u32);
} active_table SEC(".maps");

/* Epoch: index 0 -> u64 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} epoch SEC(".maps");

/* Admission mode: index 0 -> u32 (normal/soft/hard) */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u32);
} admission_mode SEC(".maps");

/* Per-route config: (vip,port,proto) -> cfg(total_slots, ...) */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 32768);
    __type(key, struct nity_route_id);
    __type(value, struct nity_route_cfg);
} route_cfg SEC(".maps");

/* Per-route deterministic gear counter: (vip,port,proto) -> u32 counter */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 32768);
    __type(key, struct nity_route_id);
    __type(value, __u32);
} route_counter SEC(".maps");

/* Conntrack stickiness (MVP): flow_key -> backend + epoch + ts */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 262144);
    __type(key, struct nity_flow_key);
    __type(value, struct nity_flow_val);
} conntrack_LRU SEC(".maps");

/* Per-CPU counters (index 0 -> struct nity_counters) */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct nity_counters);
} counters SEC(".maps");

#endif /* NITY_MAPS_H */
