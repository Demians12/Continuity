#ifndef NITY_COMMON_H
#define NITY_COMMON_H

#include <linux/types.h>

/*
 * - Keep keys in network byte order (as seen by the kernel) to avoid confusion.
 * - The agent must write keys using the same convention.
 */

#define NITY_ADMISSION_NORMAL 0u
#define NITY_ADMISSION_SOFT   1u
#define NITY_ADMISSION_HARD   2u

#define NITY_ACTIVE_TABLE_A   0u
#define NITY_ACTIVE_TABLE_B   1u

/* Minimal route id: (vip, vport, proto) */
struct nity_route_id {
    __u32 vip;      /* IPv4, network order */
    __u16 vport;    /* network order */
    __u8  proto;    /* e.g., IPPROTO_TCP */
    __u8  _pad;
};

/* Route key extends route_id with slot index */
struct nity_route_key {
    __u32 vip;      /* network order */
    __u16 vport;    /* network order */
    __u8  proto;
    __u8  _pad;
    __u32 slot;     /* 0..total_slots-1 */
};

/* Backend destination */
struct nity_backend {
    __u32 ip4;      /* IPv4, network order */
    __u16 port;     /* network order */
    __u16 id;       /* stable backend id (optional) */
    __u32 flags;    /* reserved */
};

/* Per-route config */
struct nity_route_cfg {
    __u32 total_slots;   /* S_total for that (vip,port,proto) */
    __u32 flags;         /* reserved (future: per-route policy) */
};

/*
 * Flow key (connect4 MVP): use socket cookie + route_id.
 * This yields deterministic stickiness for the lifetime of the socket.
 */
struct nity_flow_key {
    __u64 cookie;        /* bpf_get_socket_cookie(ctx->sk) */
    struct nity_route_id rid;
    __u32 _pad;          /* alignment */
};

struct nity_flow_val {
    struct nity_backend be;
    __u64 ts_ns;
    __u64 epoch;
};

#endif /* NITY_COMMON_H */
