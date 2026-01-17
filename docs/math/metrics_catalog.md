# Nity / Continuity — Metrics Catalog (RT vs Audit)

**Purpose.** This catalog defines:
- the **minimal** metrics Nity should expose
- their **type**, **unit**, **origin**, and **cardinality policy**
- which **equations** consume each metric
- where the metric is produced in the architecture (dataplane, agent, app/sidecar)

**Design principle.** Nity distinguishes:
- **RT metrics** (real-time, used by the control loop; kept local / low-latency)
- **Audit metrics** (Prometheus; low-cardinality summaries for humans)

This document prioritizes **clarity and operability** over metric volume.

---

## 0) Naming conventions

### 0.1 Prometheus (audit) naming
- prefix everything with `nity_`
- keep label cardinality bounded
- prefer `service`, `namespace`, `node` as labels
- avoid `pod` labels in Prometheus unless explicitly sampled

Examples:
- `nity_admission_mode{service,namespace}` (gauge)
- `nity_failsafe_mode{node}` (gauge)
- `nity_dp_event_cost_ns_bucket{node}` (histogram)

### 0.2 RT signals (control loop inputs)
RT signals are not required to be Prometheus metrics. They can be:
- in-memory gauges updated each tick
- ring-buffer events
- local UDP pain signals
- BPF map counters sampled by agent

Recommended naming in code:
- structured events: `rt.event.*`
- gauges: `rt.gauge.*`
- counters: `rt.counter.*`

---

## 1) Cardinality policy (hard rules)

**Audit (Prometheus) MUST be low cardinality.**
- Allowed labels: `node`, `service`, `namespace`, `mode`
- Avoid labels: `pod`, `flow`, `ip`, `user`, `route_key`, `flow_key`

**RT (flight recorder) CAN be higher detail**, but must remain bounded:
- bounded ring buffer size
- bounded sampling rate
- bounded retention window

---

## 2) Minimal operational dashboards (audit)

Even if the control loop uses RT signals, you need audit-grade summaries for:
- “what mode were we in?”
- “did we flap?”
- “what did the controller decide and why?”
- “did costs stay bounded?”

Recommended minimum charts:
- admission mode over time (normal/soft/hard)
- failsafe mode over time (normal/hold/fallback)
- epoch and flip rate
- dp cost p99
- skew
- TTF_system and min TTF_node
- guard triggers (monotonic)
- denied admissions (soft/hard)

---

## 3) Catalog table (audit + RT)

Legend:
- Type: `gauge`, `counter`, `histogram`
- Origin: `bpf`, `agent`, `app` (app/sidecar)
- Use: which equation or decision consumes it

### 3.1 Core control loop signals (per service / route group)
| Metric | Type | Unit | Origin | Cardinality | Used by |
|---|---:|---:|---|---|---|
| rt.gauge.backend_latency_p95_ms{backend} | gauge | ms | agent/app/bpf | RT-high | P_b (Eq. 2.3) |
| rt.gauge.backend_err_rate{backend} | gauge | ratio | agent/app/bpf | RT-high | P_b + abs penalty (Eq. 2.3) |
| rt.gauge.backend_queue_proxy{backend} | gauge | count | app/bpf | RT-high | P_b (Eq. 2.3) |
| rt.gauge.pressure_backend{backend} | gauge | unitless | agent | RT-high | weights (Eq. 4.1) |
| rt.gauge.weight_desired{backend} | gauge | ratio | agent | RT-high | slots target (Eq. 5.1) |
| rt.gauge.slots_cur{backend} | gauge | slots | agent/bpf | RT-high | conductance, routing |
| rt.gauge.slots_target{backend} | gauge | slots | agent | RT-high | viscosity (Eq. 5.2) |
| rt.gauge.skew | gauge | ratio | agent | RT-low | monotonic guard (Eq. 8.3) |
| nity_skew{service,namespace} | gauge | ratio | agent | Low | audit view of skew |
| nity_epoch_current{node} | gauge | count | agent/bpf | Low | coherence visibility |
| nity_epoch_flip_total{node} | counter | count | agent | Low | change frequency |
| nity_backend_set_hash{service,namespace} | gauge | hash | agent | Low | churn detection (audit) |

Notes:
- `{backend}` is RT-only; do NOT export backend labels to Prometheus by default.
- For Prometheus, aggregate per service or sample a small fixed subset.

### 3.2 Admission control (boundary protection)
| Metric | Type | Unit | Origin | Cardinality | Used by |
|---|---:|---:|---|---|---|
| rt.gauge.admission_mode{service} | gauge | enum | agent | RT-low | dataplane knob |
| nity_admission_mode{service,namespace} | gauge | enum | agent | Low | audits |
| rt.gauge.tokens_remaining{service} | gauge | tokens | agent/bpf | RT-low | token bucket (Eq. 10.1) |
| nity_tokens_remaining{service,namespace} | gauge | tokens | agent | Low | audits |
| rt.counter.admissions_allowed_total{service} | counter | count | bpf | RT-low | throughput |
| rt.counter.admissions_denied_total{service,mode} | counter | count | bpf | RT-low | backpressure effect |
| nity_admissions_denied_total{service,namespace,mode} | counter | count | agent | Low | audits |
| rt.gauge.retry_storm_index{service} | gauge | ratio | agent/app | RT-low | optional policy (RFC 0002) |

### 3.3 Failsafe and staleness
| Metric | Type | Unit | Origin | Cardinality | Used by |
|---|---:|---:|---|---|---|
| rt.gauge.agent_heartbeat_age_s{node} | gauge | s | bpf/agent | RT-low | failsafe (Eq. 11.2) |
| nity_agent_heartbeat_age_seconds{node} | gauge | s | agent | Low | audits |
| rt.gauge.failsafe_mode{node} | gauge | enum | bpf | RT-low | decision path |
| nity_failsafe_mode{node} | gauge | enum | agent/bpf | Low | audits |
| rt.counter.fallback_used_total{node,service} | counter | count | bpf | RT-low | fallback prevalence |
| nity_fallback_used_total{node} | counter | count | agent | Low | audits |

### 3.4 Dataplane cost and correctness
| Metric | Type | Unit | Origin | Cardinality | Used by |
|---|---:|---:|---|---|---|
| rt.hist.dp_event_cost_ns | histogram | ns | bpf | RT-low | invariant: bounded cost |
| nity_dp_event_cost_ns_bucket{node} | histogram | ns | agent | Low | audit p95/p99 |
| rt.counter.map_lookup_fail_total | counter | count | bpf | RT-low | invariant + debugging |
| nity_map_lookup_fail_total{node} | counter | count | agent | Low | audits |
| rt.counter.conntrack_evictions_total | counter | count | bpf | RT-low | stickiness health |
| nity_conntrack_evictions_total{node} | counter | count | agent | Low | audits |
| rt.gauge.conntrack_hit_ratio | gauge | ratio | agent | RT-low | stability signal |
| nity_conntrack_hit_ratio{service,namespace} | gauge | ratio | agent | Low | audits |

### 3.5 Node pathology signals (PSI / cgroups)
| Metric | Type | Unit | Origin | Cardinality | Used by |
|---|---:|---:|---|---|---|
| rt.gauge.psi_cpu_some_10s | gauge | ratio | agent | RT-low | stall predicate (Eq. 6.1) |
| rt.gauge.psi_cpu_full_10s | gauge | ratio | agent | RT-low | stall predicate (Eq. 6.1) |
| rt.gauge.psi_mem_full_10s | gauge | ratio | agent | RT-low | stall predicate (Eq. 6.1) |
| rt.gauge.stall_sust{node} | gauge | bool | agent | RT-low | admission policy (Eq. 9.2) |
| nity_stall_sustained{node} | gauge | bool | agent | Low | audits |
| rt.gauge.ttf_cpu_s / rt.gauge.ttf_mem_s / rt.gauge.ttf_io_s | gauge | s | agent | RT-low | TTF_r (Eq. 7.1) |
| rt.gauge.ttf_node_s{node} | gauge | s | agent | RT-low | TTF_node (Eq. 7.2) |
| nity_ttf_system_seconds | gauge | s | agent | Low | audits |

### 3.6 Reasons and controller decisions (explainability)
| Metric | Type | Unit | Origin | Cardinality | Used by |
|---|---:|---:|---|---|---|
| rt.counter.decision_reason_total{reason} | counter | count | agent | RT-low | Constitution: deterministic simplicity |
| nity_decision_reason_total{reason} | counter | count | agent | Low | audits |
| rt.counter.guard_trigger_total | counter | count | agent | RT-low | monotonic guard observability |
| nity_guard_trigger_total{service,namespace} | counter | count | agent | Low | audits |
| rt.counter.flap_events_total{kind} | counter | count | agent | RT-low | stability check |
| nity_flap_events_total{kind} | counter | count | agent | Low | audits |

---

## 4) Mapping metrics -> equations (quick index)

- EWMA / derivative:
  - consumes: any RT gauge you smooth
- Pressure P_b:
  - consumes: `backend_latency_p95_ms`, `backend_err_rate`, `backend_queue_proxy`
  - produces: `pressure_backend`
- Weights w_b:
  - consumes: `pressure_backend`
  - produces: `weight_desired`
- Slots:
  - consumes: `weight_desired`, `S_total`
  - produces: `slots_target`, `slots_cur`
- Skew + guard:
  - consumes: `reqs_total`, `weight_desired`
  - produces: `skew`, `guard_trigger_total`
- Conductance G:
  - consumes: `slots_cur`, `pressure_backend`
- Admission modes:
  - consumes: `TTF_system`, `stall_sust`, `G`
  - produces: `admission_mode`, `tokens_remaining`, deny counters
- Failsafe:
  - consumes: `agent_heartbeat_age_s`
  - produces: `failsafe_mode`, `fallback_used_total`
- Invariants harness:
  - consumes: dp cost histogram, deny counters, skew, flap events, remap percent (when added)

---

## 5) What NOT to export to Prometheus (by default)

Avoid exporting these as Prometheus labels:
- backend ID / pod name
- IP addresses, ports
- route_key / flow_key
- raw slot indices
- per-flow events

Instead:
- keep them in RT flight recorder
- provide CLI tooling (`nityctl`) to dump map snapshots on demand

---

## 6) Aesthetic guidelines (so people don’t get confused)

- Always include units in metric names where possible:
  - `_ms`, `_ns`, `_seconds`, `_bytes`
- For enums, publish a legend in docs:
  - admission_mode: 0 normal, 1 soft, 2 hard (or use labels but keep stable)
- If you must expose backend-level Prometheus metrics:
  - sample N backends deterministically (e.g., lowest hash IDs)
  - or expose top-k by pressure (bounded k)
  - never “all pods” by default

---

## 7) Minimal “getting started” subset (audit)

If you only ship 10 Prometheus metrics initially, choose these:

1) `nity_admission_mode`
2) `nity_failsafe_mode`
3) `nity_agent_heartbeat_age_seconds`
4) `nity_epoch_current`
5) `nity_epoch_flip_total`
6) `nity_dp_event_cost_ns_bucket`
7) `nity_skew`
8) `nity_guard_trigger_total`
9) `nity_admissions_denied_total{mode}`
10) `nity_ttf_system_seconds`

Everything else can live in RT events until you have a proven dashboard need.

---

**End of metrics catalog.**
