# RFC 0004 — Dataplane Contract (Maps, Keys, Shared Structs)

- **Status:** Draft (MVP)
- **Owner:** Nity / Continuity
- **Scope:** The stable contract between agent and eBPF dataplane

---

## 1. Context

Nity is a dataplane/control-plane system. The agent and dataplane must agree on:
- keys (`route_key`, `flow_key`)
- map semantics (A/B slot tables, conntrack, control knobs)
- shared structs layout and versioning

Without an explicit contract, changes become implicit and fragile.

---

## 2. Goals (MVP)

1) Define minimal maps required for MVP behaviors:
   - deterministic selection
   - stickiness
   - atomic epoch flip
   - admission mode
   - failsafe heartbeat
2) Define stable key formats and versioning rules.
3) Keep dataplane operations O(1).

---

## 3. Non-goals (MVP)

- Full IPv6 support (design should be extensible).
- L7 metadata and in-band telemetry headers.
- High-cardinality metrics in Prometheus.

---

## 4. Keys

### 4.1 route_key (control volume + slot)
Route key must uniquely identify the field location:
```text
route_key = hash(vip, vport, proto, slot)
```

Where:
- `vip`: service IP (or equivalent)
- `vport`: service port
- `proto`: L4 protocol id
- `slot`: integer in `[0..S_total-1]`

### 4.2 flow_key (stickiness identity)
Flow key identifies a connection for conntrack / fallback determinism:
```text
flow_key = hash(src_ip, src_port, dst_ip, dst_port, proto)
```

MVP note:
- prefer full 5-tuple when available; if a hook cannot access all fields, document the reduction clearly.

---

## 5. Shared control struct (rt_control)

A minimal control struct per route group (keyed by `route_group_key = hash(vip,port,proto)`):

Fields (conceptual):
- `schema_version`
- `admission_mode` (normal/soft/hard)
- token bucket: `(tokens, refill_rate, burst)`
- optional: `backend_set_hash` (churn detection)
- optional: `policy_flags` (future-proofing)

---

## 6. Required maps (MVP)

### 6.1 Slot tables (A/B)
- `slot_table_A[route_key] -> backend_id`
- `slot_table_B[route_key] -> backend_id`

Properties:
- fixed-size values
- written only by agent
- read by dataplane

### 6.2 Active table selector + epoch
- `active_table -> {A|B}`
- `epoch -> u64`

Properties:
- updated atomically by agent during flip
- dataplane reads once per decision

### 6.3 Conntrack LRU
- `conntrack_lru[flow_key] -> {backend_id, last_seen_ts, epoch_seen(optional)}`

Properties:
- dataplane read/write O(1)
- eviction is bounded by LRU size

### 6.4 Agent heartbeat
- `last_agent_seen_ts -> u64`

Properties:
- agent updates periodically
- dataplane derives failsafe mode from age

### 6.5 Control map (rt_control)
- `rt_control[route_group_key] -> control_struct`

Properties:
- agent updates when mode/budgets change
- dataplane reads on admission/selection path

### 6.6 Counters (per-CPU where possible)
- `reqs_total`, `errs_total`
- `conntrack_evictions`
- `map_lookup_fail_total`
- optional: sampled latency histogram

---

## 7. Fallback backend sets (MVP options)

Fallback requires a per route group list of backends.

Option A (explicit array-like maps):
- `fallback_size[route_group_key] -> N`
- `fallback_backends[(route_group_key, idx)] -> backend_id`

Option B (reuse slot table as backend pool):
- store a compact list in a separate map for fallback only

MVP recommendation: Option A for clarity and auditing.

---

## 8. Versioning rules

- Every shared struct must include `schema_version`.
- Agent must refuse to run if dataplane expects a different **major** schema.
- Minor schema upgrades must preserve backward compatibility.

---

## 9. Security notes (MVP)

- Agent must be the only writer to slot tables and control maps (RBAC + mount permissions).
- Heartbeat and mode changes should be audit logged (RT flight recorder and Prometheus counters).

---

## 10. Acceptance criteria

- Dataplane event path does:
  - at most O(1) map lookups,
  - no loops over backends,
  - no allocations.
- Agent can rebuild inactive tables without interfering with dataplane reads.
- Fallback always selects within the correct route group.

---

## 11. References

- Control loop: RFC 0001
- Backpressure: RFC 0002
- Epoch flip and failsafe: RFC 0003
