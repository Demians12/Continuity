#ifndef NITY_METRICS_H
#define NITY_METRICS_H

#include <linux/types.h>

struct nity_counters {
    __u64 connect_events;

    __u64 admission_hard_denies;

    __u64 route_cfg_miss;
    __u64 route_counter_miss;

    __u64 conntrack_hits;
    __u64 conntrack_misses;
    __u64 conntrack_updates;

    __u64 slot_lookups;
    __u64 slot_lookup_fails;

    __u64 redirects;
};

#endif /* NITY_METRICS_H */
