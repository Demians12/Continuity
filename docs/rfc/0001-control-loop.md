# RFC 0001 — Nity Control Loop (Agent): Computing the Field

- **Status:** Draft (MVP)
- **Owner:** Nity / Continuity
- **Scope:** Control-plane behavior (agent) that computes and projects the Eulerian field
- **Audience:** Contributors implementing `nity-agent` and its tests

---

## 1. Context

Nity treats Kubernetes service traffic as **fluid** and backends as fixed points on a mesh. The dataplane (eBPF) must stay **O(1)** per connect/event, while a userspace agent periodically computes a **field** (slot allocation) and projects it atomically to the dataplane via **A/B tables + epoch flip**.

This RFC defines the **agent control loop**:
- what it reads (signals),
- what it computes (pressure, weights, slots),
- how it damps oscillations (viscosity, hysteresis),
- how it prevents harmful steps (monotonic skew guard),
- and how it updates the dataplane safely (delegated to RFC 0003).

---

## 2. Goals (MVP)

1) Provide a deterministic, explainable loop that computes per-backend slot allocation.  
2) Use **fast** (ms–s) signals for decisions; Prometheus is **audit**, not primary sensing.  
3) Avoid oscillation (flapping) with **viscosity + hysteresis**.  
4) Bound the impact of bad steps (monotonic skew guard).  
5) Produce a coherent field projection that the dataplane can consume safely.

---

## 3. Non-goals (MVP)

- “Perfect routing” across different services (no cross-service routing).
- Automatically guaranteeing capacity (if there is no spare backend capacity, the agent must move to admission control and/or emit scale hints).
- L7 request-level scheduling (MVP acts at L4 connect; higher-level signals may be added later).
- Global coordinator/leader election (MVP is local-first; cluster-wide coordination can be added later).

---

## 4. Terminology

- **Route group / control volume:** a service boundary identified by `(VIP, port, proto)` (or equivalent).  
- **Equivalent backend set:** the set of backends that can legally serve that route group (same semantics, same selector).  
- **Field:** slot table + active table + epoch + control knobs (admission/failsafe).  
- **Tick:** the agent loop period `Δt`.

---

## 5. Inputs (signals)

### 5.1 Fast signals (recommended MVP)
- **Backend service signals** (per backend `b`):
  - `p95_latency_b`
  - `err_rate_b`
  - optional: `queue_depth_b` or a queue proxy
- **Node signals**:
  - PSI (`/proc/pressure/*`) for cpu/mem/io
  - cgroup usage/limits for CPU/mem/io where applicable
- **Dataplane counters** (from BPF maps):
  - per-backend `reqs`, `errors`, sampled lat histogram (if present)
  - conntrack evictions
  - map lookup failures

### 5.2 Audit signals (Prometheus)
Low-cardinality summaries only (e.g., per service, per node). Prometheus is not required for the control step.

---

## 6. Core computations

### 6.1 Filtering (EWMA)
```text
ewma(x,t) = α*x(t) + (1-α)*ewma(x,t-Δt)
```

### 6.2 Robust derivative (positive consumption only)
Used for “time to failure” calculations.
```text
v_x(t) = max(0, (ewma(x,t) - ewma(x,t-Δ)) / Δ )
```

### 6.3 Normalization
```text
x*(t) = x(t) / x_ref
```

### 6.4 Backend pressure (MVP)
Pressure is a composite resistance to flow.
```text
P_b = W_Q*Q_b* + W_L*L_b* + W_E*E_b*
Q_b* = Q_b / Q_ref
L_b* = p95_latency_b / L_ref
E_b* = clip(err_rate_b / E_ref, 0, E_max)

Optional absolute error penalty:
if err_rate_b > η then P_b = P_b + K_E
```

### 6.5 Convert pressure to desired weights
Inverse pressure means healthier backends receive more flow.
```text
w_b = (1 / (P_b + ε)) / sum_i (1 / (P_i + ε))
```

### 6.6 Slots target
```text
s_target_b = round( w_b * S_total )
```

### 6.7 Viscous update (slew-rate)
Slots cannot change abruptly.
```text
Δs_b = clip(s_target_b - s_cur_b, -Δmax, +Δmax)
s_new_b = s_cur_b + Δs_b
```

### 6.8 Skew metric and monotonic guard
Observed share should converge to desired weights, not worsen.
```text
share_b = reqs_b / (sum_i reqs_i + ε)
skew    = max_b | share_b - w_b |

Monotonic guard:
if skew(t+1) > skew(t) + ε_skew:
  reduce Δmax := k*Δmax   (0<k<1)
  or HOLD allocation step for this tick
```

---

## 7. Regimes vs States (control outputs)

This RFC defines the *loop*, not the full regimes guide. The loop outputs state knobs used by dataplane.

The agent may compute:
- `regime ∈ {laminar, transition, crisis, recovery}` (high-level posture)

And sets concrete states:
- `admission_mode ∈ {normal, soft, hard}` (see RFC 0002)

`failsafe_mode` is dataplane-derived (see RFC 0003) but influenced by agent heartbeat freshness.

Recommended mapping (MVP):
- laminar -> admission normal
- transition -> admission normal or soft
- crisis -> admission hard
- recovery -> admission soft then normal with timers/hysteresis

---

## 8. Backend equivalence and the monopoly regime

The loop MUST compute allocations **only within the equivalent backend set** for a route group.

If `N_backends == 1`:
- slot allocation is trivial (all slots go to the only backend),
- the correct continuity action under stress is **admission control** (RFC 0002),
- and (optional) emit a scale hint (operator / controller extension).

This prevents “fake rerouting” to unrelated services.

---

## 9. Table build strategy (MVP)

MVP table build can be simple and deterministic:
- compute `s_new_b` for each backend
- fill the inactive slot table with backend IDs replicated by `s_new_b`
- slot order should be deterministic to avoid noise (stable iteration order)

Future: upgrade to weighted consistent mapping (Maglev-like) to bound remaps under churn.

---

## 10. Tick scheduling and jitter

The loop runs every `Δt` (e.g., 200ms). Use small deterministic jitter to avoid phase locking with periodic workloads.

```text
tick_time = Δt + jitter(seed, tick_index)   where jitter is bounded (e.g., ±5%)
```

---

## 11. Outputs (what the agent writes)

Per route group / service:
- slot table (inactive A/B)
- flip epoch (see RFC 0003)
- `rt_control` knobs (admission mode, budgets, thresholds as needed)
- agent heartbeat timestamp (for failsafe thresholds)

Agent also writes:
- flight-recorder events (RT, local) for debugging
- low-cardinality audit metrics (Prometheus)

---

## 12. Acceptance criteria (MVP)

Functional:
- Slot allocation changes are bounded by `Δmax` per tick.
- Under stable topology and non-overload, `skew` should not worsen repeatedly.

Stability:
- No persistent flap: admission and slot changes exhibit hysteresis.

Safety:
- No partial table exposure (guaranteed by RFC 0003 mechanisms).

---

## 13. Open questions (tracked)

1) Minimum viable backend signals: can we rely only on kernel+BPF counters at first?  
2) Queue proxy: how do we derive it if app queue depth is not available?  
3) Consistent mapping upgrade: when do we switch from replication to Maglev-like?  
4) Scale hint format: CRD vs event vs annotation (future operator).

---

## 14. References

- Regimes guide: `docs/guide/regimes.md`
- States guide: `docs/guide/states.md`
- Backpressure: RFC 0002
- Epoch flip and failsafe: RFC 0003
- Dataplane contract: RFC 0004
