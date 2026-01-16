# Nity / Continuity — Hydraulic Constitution v5.0 (Full, 24 Articles)
> **Format:** descriptive + operational (description, metrics, equations)  
> **Scope:** Kubernetes traffic continuity via **eBPF dataplane** + **deterministic control-plane**  
> **Rendering note:** Many Markdown viewers do **not** support LaTeX/MathJax.  
> This file uses **plain-text equations** inside fenced code blocks for universal readability.

---

## Preamble — Axiom Zero (The Eulerian Indifference)
Nity does not “see” or “care” about the individual request/user at the decision layer.  
The decision layer is **Eulerian**: it observes **pressure fields** and **flow vectors** at fixed points (backend slots), not per-particle trajectories.

- **Constraint:** Lagrangian tracking (per-request state in the decision plane) is forbidden due to explosive cost and instability.
- **Allowance:** the dataplane may maintain minimal per-flow state **only** to preserve continuity (stickiness) and bounded remaps.

---

## Global Notation (Used Across Articles)

### Time & filtering
- Control tick: **Δt** (typ. 50–500ms)
- Robust-derivative window: **Δ** (typ. 2–10s)

**EWMA**
```text
x_tilde(t) = α*x(t) + (1-α)*x_tilde(t-Δt)
```

**Robust derivative (positive consumption only)**
```text
v_x(t) = max(0, (x_tilde(t) - x_tilde(t-Δ)) / Δ )
```

### Normalization
```text
x*(t) = x(t) / x_ref
```

### Resource pathology gate (PSI)
For a resource r ∈ {cpu, mem, io} and a window W:
```text
stall(W) =
  1 if PSI_full_r(W) > θ_full_r
  OR
  1 if PSI_some_r(W) > θ_some_r
  else 0
```

Sustained stall over discrete samples:
```text
stall_sust =
  1 if sum_{k=1..m} stall(t - k*Δt) >= m*p
  else 0
```

### Minimal backend pressure (MVP-compatible)
```text
P_b = W_Q * Q*_b + W_L * L*_b + W_E * E*_b

Q*_b = Q_b / Q_ref
L*_b = p95_lat_b / L_ref
E*_b = clip(err_rate_b / E_ref, 0, E_max)
```

Optional “absolute” error penalty:
```text
if err_rate_b > η:
  P_b = P_b + K_E
```

### Slot targets (inverse pressure) and viscous update
Desired weight:
```text
w_b = (1/(P_b+ε)) / sum_i (1/(P_i+ε))
```

Target slots:
```text
s_target_b = round(w_b * S_total)
```

Slew-rate (viscosity):
```text
Δs_b = clip(s_target_b - s_cur_b, -Δ_max, +Δ_max)
s_new_b = s_cur_b + Δs_b
```

### Skew and monotonic guard
Observed share:
```text
share_b = reqs_b / (sum_i reqs_i + ε)
```

Skew:
```text
skew = max_b |share_b - w_b|
```

Guard:
```text
if skew(t+1) > skew(t) + ε_guard:
  Δ_max = k * Δ_max    (k < 1)
  OR HOLD (freeze updates temporarily)
```

### Conductance proxy and admission modes (backpressure)
```text
G = sum_b (slots_b / (P_b + ε))
```

Mode selection (example):
```text
normal: (TTF_system >= T_safe) and (stall_sust == 0) and (G >= G_min)
soft:   (TTF_system <  T_safe) or  (G <  G_min)
hard:   (TTF_system <  T_hard) or  (stall_sust == 1)
```

---

# Articles 1–24
Each article contains:
- **Description** (what it means + why it exists)
- **Metrics** (real-time internal vs audit)
- **Equations** (plain-text grounding)

---

## Article 1 — O(1) Dataplane (Bounded Cost per Event)
### Description
The dataplane must remain **O(1)** per packet/connection event. No per-backend scans, no loops over N backends, no dynamic allocations.

### Metrics
- **RT (internal):** `dp_event_cost_ns` (ring-buffer sampled), `dp_map_lookup_fail_total`
- **Audit:** `dp_event_cost_ns_p50/p95/p99`, `dp_map_lookup_fail_total`

### Equations
```text
cost_event = t_end - t_start
p99(cost_event) <= B_ns
```

---

## Article 2 — Full Physical Key (No Control-Volume Collisions)
### Description
Each control volume is uniquely identified by physical boundary: **VIP + port + protocol (+ slot)**.

### Metrics
- **RT:** `route_keys_active`, `route_key_collision_total`
- **Audit:** collision rate over time

### Equations
```text
route_key = hash(vip, vport, proto, slot)
collision_rate = collisions / (lookups + ε)
```

---

## Article 3 — Atomic Field Projection by Epoch (A/B Tables + Flip)
### Description
Dataplane must never observe a half-updated field. Control-plane writes inactive table then flips `active_table` and increments `epoch`.

### Metrics
- **RT:** `epoch_current`, `epoch_flip_total`, `agent_heartbeat_age_seconds`
- **Audit:** epoch flip counts, staleness trends

### Equations
```text
staleness = now - last_heartbeat
flip_allowed if staleness <= τ_ok
```

---

## Article 4 — Laminar Gear + Real Stickiness (Deterministic Selection + Conntrack)
### Description
No randomness in the normal regime. First selection is deterministic; then conntrack preserves flow continuity.

### Metrics
- **RT:** `conntrack_hit_ratio`, `flow_remap_total{reason}`, `slot_pick_total{backend}`
- **Audit:** hit ratio trends, remap reasons distribution

### Equations
```text
slot = counter mod S_total_slots

hit_ratio = hits / (hits + misses + ε)
remap_rate = remaps / (flows_sampled + ε)
```

---

## Article 5 — Topological Continuity (Weighted Consistent Mapping Under Churn)
### Description
Topology changes must not reshuffle the universe. Remaps should be local and predictable.

### Metrics
- **RT:** `backend_set_hash`, `table_rebuild_ms`
- **Audit:** `remap_percent_on_churn`, `table_rebuild_ms_p95`

### Equations
```text
remap_% = changed_flows / (flows_sampled + ε) * 100
policy: remap_% <= ρ_max for small churn
```

---

## Article 6 — Inertial Fail-Safe (HOLD → FALLBACK)
### Description
If the agent dies, dataplane remains coherent: HOLD first, then FALLBACK deterministic selection.

### Metrics
- **RT:** `failsafe_mode{normal,hold,fallback}`, `agent_heartbeat_age_seconds`
- **Audit:** mode time-series

### Equations
```text
age = now - last_heartbeat

mode =
  normal   if age < τ1
  hold     if τ1 <= age < τ2
  fallback if age >= τ2
```

---

## Article 7 — Layered Pressure (Backend vs Path vs Node)
### Description
Pressure is decomposed to avoid false positives: backend, path, and node.

### Metrics
- **RT:** `pressure_backend{b}`, `pressure_path{b}`, `pressure_node{n}`
- **Audit:** per service/node summaries

### Equations
```text
P_backend(b) = WQ*Q*(b) + WL*L*(b) + WE*E*(b) + WC*C*(b)

P_node(n) = Wpsi*PSI*(n) + Wfd*FD*(n) + Wct*CT*(n) + Wio*IO*(n)
```

---

## Article 8 — Control Viscosity (Slew-Rate + Hysteresis + Filtering)
### Description
Field does not react to noise. Filtering, slew-rate, and hysteresis prevent flapping.

### Metrics
- **RT:** `slots_delta_per_cycle`, `flap_events_total`
- **Audit:** `slots_delta_p95`, flap rate per minute

### Equations
```text
Δs_b = clip(s_target_b - s_cur_b, -Δ_max, +Δ_max)

hysteresis example:
  apply updates only if |P_b - P_prev_b| > δP for at least T_hold
```

---

## Article 9 — Monotonic Convergence (Must Not Worsen Skew)
### Description
A control step must not worsen skew under stable topology; otherwise shrink the step or HOLD.

### Metrics
- **RT:** `skew_before`, `skew_after`, `monotonic_guard_trigger_total`
- **Audit:** settling time, skew trends

### Equations
```text
skew = max_b |share_b - w_b|
if skew_next > skew_now + ε_guard:
  Δ_max = k*Δ_max  OR HOLD
```

---

## Article 10 — Selective Permeability (Drip + Cooldown)
### Description
Sick backends get diagnostic drip (token bucket) + exponential cooldown on failures.

### Metrics
- **RT:** `drip_tokens{b}`, `drip_allow_total{b}`, `probe_success_ratio{b}`
- **Audit:** drip rate, cooldown durations

### Equations
```text
tokens_b(t) = min(B, tokens_b(t-Δt) + r*Δt) - used

cooldown_{k+1} = min(C_max, γ*cooldown_k)
```

---

## Article 11 — Evacuation Principle (Long-Lived Flows Under Degradation)
### Description
Degraded backend reduces internal pressure by staged draining (stop-new → max-conn-age → shed).

### Metrics
- **RT:** `active_conns{b}`, `conn_age_p95{b}`, `shed_total{policy}`
- **Audit:** shed events, retry impact

### Equations
```text
MCA = clip(MCA0 * (TTF / TTF_ref), MCA_min, MCA_max)
```

---

## Article 12 — Temporal Prediction (TTF)
### Description
Health is time remaining to loss of control.

### Metrics
- **RT:** `ttf_resource_seconds{r}`, `ttf_node_seconds`, `ttf_system_seconds`
- **Audit:** TTF minima and breach events

### Equations
```text
R_r = L_r - U_r
V_r = v_{U_r}(t)
TTF_r = R_r / max(ε, V_r)

TTF_node   = min_r TTF_r
TTF_system = min(TTF_node, TTF_backend_or_system)
```

---

## Article 13 — Stall Filter (PSI Defines Pathology)
### Description
Usage matters only if it causes stalls; PSI is the kernel progress indicator.

### Metrics
- **RT:** `psi_*_some_10s/60s`, `psi_*_full_10s/60s`, `stall_sust`
- **Audit:** PSI trend charts, stall breach counts

### Equations
```text
stall(W) = 1 if PSI_full(W) > θ_full OR PSI_some(W) > θ_some else 0
stall_sust = sustained stall over discrete samples
```

---

## Article 14 — Global Backpressure (Circuit Breaker)
### Description
System survival outranks external demand; boundary admission switches normal/soft/hard.

### Metrics
- **RT:** `admission_mode{normal,soft,hard}`, `reject_total{class}`, `delay_injected_ms{class}`
- **Audit:** mode time-series, rejection ratios

### Equations
```text
G = sum_b (slots_b / (P_b + ε))

normal if (TTF_system >= T_safe) and (stall_sust == 0) and (G >= G_min)
soft   if (TTF_system <  T_safe) or  (G <  G_min)
hard   if (TTF_system <  T_hard) or  (stall_sust == 1)

optional:
RSI = (retries/sec) / (success/sec + ε)
```

---

## Article 15 — Local Sovereignty (Node Immune System)
### Description
The agent serves its own node first; if the node is unsafe, it fails readiness to shed load.

### Metrics
- **RT:** `local_readiness_state`, `readiness_fail_total{reason}`
- **Audit:** readiness flaps, reason distribution

### Equations
```text
ready =
  1 if (TTF_node > T_min) and (stall_sust == 0) and (FD* < μ) and (CT* < ν)
  else 0
```

---

## Article 16 — Pain Signaling (Event-Driven, Ratelimited)
### Description
Silence is normal; send pain only on state transitions (with auth + rate limits).

### Metrics
- **RT:** `pain_signal_sent_total`, `pain_signal_drop_total{auth,replay,ratelimit}`
- **Audit:** pain events per node, drop reasons

### Equations
```text
send = 1 if state(t) != state(t-Δt) else 0
rate limiting: token bucket (same structure as drip)
```

---

## Article 17 — Fear Factor (Bounded Empathy With Decay)
### Description
Neighbor distress tightens local thresholds, bounded and decaying back to baseline.

### Metrics
- **RT:** `fear_factor_active`, `fear_delta_threshold_pct`
- **Audit:** cascade cap hits, fear durations

### Equations
```text
Δθ = min(θ_max_drop, k*severity)

θ(t) = θ0 * (1 - Δθ * exp(-(t - t0)/τ))

anti-cascade cap:
  Δθ <= Δθ_cap if too many neighbors are red
```

---

## Article 18 — Stigmergy (In-Band Telemetry With Staleness Rules)
### Description
State travels with the traffic. In-band telemetry must be versioned and time-bounded.

### Metrics
- **RT:** `inband_parse_fail_total`, `inband_stale_drop_total`
- **Audit:** schema versions seen, drop rates

### Equations
```text
stale = 1 if (now - ts) > TTL else 0

accept =
  1 if (schema_ver is supported) and (stale == 0)
  else 0
```

---

## Article 19 — Reaction Ladder (Three-Stage Crisis Protocol)
### Description
Actions follow a cost hierarchy: (1) traffic, (2) resources, (3) territory (nodes/cluster).

### Metrics
- **RT:** `reaction_stage{1,2,3}`, `scaling_action_total{type}`
- **Audit:** action outcomes, stage distribution

### Equations (example rules)
```text
stage =
  1 if (TTF_backend < T_scale) and (stall_sust == 0)
  2 if (stall_sust == 1) and (mem/io dominates)
  3 if (G < G_min) and (no local headroom)
```

---

## Article 20 — Surge Tank (Bounded Shock Absorber During Scaling)
### Description
Temporary buffering of handshakes during scaling is allowed but must be bounded and RSI-aware.

### Metrics
- **RT:** `handshake_buffer_depth`, `handshake_wait_ms_p95`, `handshake_drop_total`
- **Audit:** wait distributions, drops vs RSI

### Equations
```text
depth <= D_max

drop =
  1 if depth >= D_max OR RSI > r_max
  else 0

wait = t_accepted - t_arrived
```

---

## Article 21 — Proportional Drainage (Queue Debt Must Be Paid)
### Description
Backlog is “debt”. Pressure includes debt clearance time to avoid flooding new capacity.

### Metrics
- **RT:** `queue_debt{b}`, `time_to_clear_debt_seconds{b}`
- **Audit:** debt clearance curves

### Equations
```text
debt = max(0, Q_accum)

T_drain = debt / (drain_rate + ε)

P_real = (traffic + debt/T_drain) / capacity
```

---

## Article 22 — Identity & Security (Physics Cannot Violate Law)
### Description
Field decisions are subordinate to security policies (labels, network policies). No bypass.

### Metrics
- **RT:** `policy_denies_total{reason}`, `unauthorized_flow_attempt_total`
- **Audit:** denies by namespace/service, drift detection

### Equations
```text
allow = 1 if policy(src, dst, svc) == allow else 0
```

---

## Article 23 — Deterministic Simplicity (Every Action Has a Physical Cause)
### Description
Every action must map to explicit physical reasons. No black-box decisions.

### Metrics
- **RT:** `decision_reason_total{reason}`, `decision_event_rate`
- **Audit:** reason distribution; unknown reason must be 0

### Equations (example)
```text
score_ttf  = 1 if TTF_system < T_safe else 0
score_psi  = stall_sust
score_err  = 1 if err_rate > η else 0
score_skew = 1 if skew > skew_max else 0

reason = argmax_k(score_k)  for k ∈ {ttf, psi, err, skew}
```

---

## Article 24 — Invariant Harness (Physics CI: Sim + Replay)
### Description
Ship a harness that validates invariants under simulation and replay (no-blackhole, bounded overhead, monotonic skew, bounded remap).

### Metrics
- **RT:** `invariant_fail_fast_total{test}` (optional)
- **Audit:** `invariant_pass{test}`, `max_remap_pct`, `max_flap_rate`, `dp_cost_p99`

### Equations (examples)
```text
bounded dataplane:
  p99(dp_event_cost) <= B_ns

monotonic skew (stable topology):
  skew(t+1) <= skew(t) + ε_guard

no-blackhole post-admission:
  admitted == delivered + explicitly_rejected
```

---

## Appendix A — Metric Conventions (Recommended Names)
- RT (in-memory / flight recorder): structured events or `rt_*`
- Audit (Prometheus): low-cardinality counters/gauges/histograms
  - `nity_epoch_current`
  - `nity_admission_mode`
  - `nity_ttf_system_seconds`
  - `nity_dp_event_cost_ns_bucket`
  - `nity_skew`
  - `nity_guard_hits_total`
  - `nity_reject_total{class}` (keep class cardinality bounded)

---

## Appendix B — Defaults (Good Starting Points)
These are starting points, not truth.

```text
Δt: 200ms
Δ:  5s
Δ_max (slots/cycle): 1–3 per backend
τ1 (HOLD): 2–5s
τ2 (FALLBACK): 10–20s
PSI windows: 10s and 60s
T_safe: 60–180s
T_hard: 10–30s
```

---

**End of Constitution**
