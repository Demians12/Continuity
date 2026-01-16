# Continuity Constitution
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

Extended weight (when capacity, topology, and dependency pressure are modeled):
w_b =
  (capacity_factor_b / ((P_total_b + ε) * topo_penalty_b))
  /
  sum_i (capacity_factor_i / ((P_total_i + ε) * topo_penalty_i))

where:
  topo_penalty_b = 1 + λ_topo * topology_cost_b
  P_total_b = P_backend(b) + W_dep * P_dependency(service)   (if dependency pressure is used)

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

### Real-World Amendment — The “Particle” Might Be a Connection (HTTP/2, gRPC, keep-alive)
#### Description
In many modern systems, a single connection can carry many requests (multiplexing).
If Nity only reacts when **new connections** appear, it may react late when load spikes **inside existing connections**.
This is not a failure — it is a limit of the observation point.
The constitution makes the limit explicit and defines a simple fix: **use a lightweight load estimate** (requests/s if visible, otherwise bytes/s).

#### Metrics
- **RT:** `requests_per_connection{backend}` (if available), `bytes_per_connection{backend}`, `multiplexing_factor{service}`
- **Audit:** histograms/quantiles of requests per connection (or bytes per connection)

#### Equations
```text
if requests are observable (e.g., in-band header, sidecar, or app signal):
  load_est = requests_per_sec
else:
  load_est = bytes_per_sec

requests_per_connection = requests_per_sec / (new_connections_per_sec + ε)

multiplexing_factor = clip(requests_per_connection / R_ref, 0, M_max)

rule:
  if multiplexing_factor is high:
    do not rely only on connect() events for control decisions
    incorporate load_est into pressure and admission decisions
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

### Real-World Amendment — Equivalent Backend Sets and Monopoly Mode
#### Description
Nity can only “redistribute” traffic **within a set of backends that can truly do the same job**.
A pod for *serviceA* cannot serve *serviceB*.
If there is only **one** backend in the equivalent set, there is **no alternate internal route**.
In that case Nity enters **Monopoly Mode**:
- preserve what already entered;
- reduce what is still trying to enter (backpressure);
- optionally emit a **ScaleHint** (a request for more capacity) if policy allows.

#### Metrics
- **RT:** `backend_set_size{service}`, `monopoly_mode{service}`, `scale_hint_total{service,reason}`
- **Audit:** `nity_backend_set_size{service}`, `nity_monopoly_mode{service}`, `nity_scale_hint_total{reason}`

#### Equations
```text
backend_set_size(service) = |B_service|

redistribution_possible = 1 if backend_set_size(service) >= 2 else 0

monopoly_mode(service) = 1 - redistribution_possible

if monopoly_mode == 1:
  - slots cannot "migrate" to another backend (there is none)
  - the field may only:
      (a) apply backpressure (Article 14)
      (b) request capacity via ScaleHint (Article 19)
```

### Real-World Amendment — Rollout / High Churn Mode
#### Description
During rollouts, pods appear and disappear quickly. If the controller tries to optimize aggressively while the ground is moving,
it can flap, remap too much, and create turbulence.
When churn is high, the goal becomes: **do not oscillate and do not make things worse**.

#### Metrics
- **RT:** `backend_set_changes_per_min{service}`, `rollout_mode{service}`, `remap_percent{service}`
- **Audit:** time spent in rollout mode, remap percent during rollout

#### Equations
```text
churn_rate = backend_set_changes / minute
rollout_mode = 1 if churn_rate > churn_threshold else 0

when rollout_mode == 1:
  Δ_max = min(Δ_max, Δ_rollout)
  T_hold = max(T_hold, T_hold_rollout)
  ramp-up rules for newborn backends (Article 10 amendment) are mandatory

remap_percent(service) = (flows_remapped / (flows_sampled + ε)) * 100
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

### Real-World Amendment — Replicas Are Not Truly Identical (Heterogeneity)
#### Description
In real clusters, two replicas of the “same” app can behave differently: noisy neighbors, throttling, cold caches, slower disks, etc.
Nity models this with a simple **capacity factor** per backend, derived from observed completions.

#### Metrics
- **RT:** `service_rate_rps{backend}`, `capacity_factor{backend}`
- **Audit:** distribution of capacity factors per service

#### Equations
```text
mu_b = completed_requests_in_window / window_seconds
mu_ref = median(mu_i for i in equivalent_backends)

capacity_factor_b = clip(mu_b / (mu_ref + ε), cap_min, cap_max)

use (together with pressure):
  weight_b ∝ capacity_factor_b / (P_total_b + ε)
```

### Real-World Amendment — Locality and Topology Cost (Not All Routes Cost the Same)
#### Description
Moving traffic "far" can be expensive: higher latency, more retransmits, crossing zones, or hitting cost boundaries.
Nity prefers local routes when possible and only crosses topological borders when necessary.

#### Metrics
- **RT:** `topology_cost{backend}` (e.g., 0 same node, 1 same zone, 2 other zone), `locality_ratio{service}`
- **Audit:** locality ratio over time, topology cost aggregates

#### Equations
```text
topo_penalty_b = 1 + λ_topo * topology_cost_b

use (together with pressure):
  weight_b ∝ 1 / ((P_total_b + ε) * topo_penalty_b)

locality_ratio(service) = local_traffic / (total_traffic + ε)
```

### Real-World Amendment — Dependency Pressure (The Pod Is Fine, the World Is Not)
#### Description
Sometimes the app slows down because a dependency (database, queue, external API) slows down.
The pod is not “broken” — it is being forced to wait.
Nity separates this signal so it can:
- avoid blaming the wrong backend;
- apply backpressure early to prevent queues/retry storms;
- expose a clear “external pressure” metric.

#### Metrics
- **RT:** `dependency_timeout_rate{service}`, `dependency_error_rate{service}`, `dependency_wait_ms_p95{service}`, `pressure_dependency{service}`
- **Audit:** dependency pressure time-series, dependency collapse events

#### Equations
```text
TO* = timeout_rate / (TO_ref + ε)
ER* = error_rate   / (ER_ref + ε)
WT* = wait_p95_ms  / (WT_ref + ε)

P_dependency(service) = W_to*TO* + W_er*ER* + W_wt*WT*

used together with backend pressure:
  P_total_b = P_backend(b) + W_dep * P_dependency(service)
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

### Real-World Amendment — Birth Ramp (Ready Does Not Mean Fully Capable)
#### Description
A backend can be marked Ready and still not be ready for full load (cold caches, warmup, connection pools, runtime stabilization).
New backends enter the field as “pipes still filling”: they start with low flow and ramp up.

#### Metrics
- **RT:** `backend_age_seconds{backend}`, `warmup_ramp_factor{backend}`, `warmup_duration_seconds{service}`
- **Audit:** time to reach ramp=1, failures during warmup

#### Equations
```text
age_seconds = now - backend_start_time

ramp_factor = clamp(age_seconds / T_warmup, 0, 1)

slots_target_effective = round(slots_target * ramp_factor)

used together with viscosity (Article 8):
  Δs_b = clip(slots_target_effective - s_cur_b, -Δ_max, +Δ_max)
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

### Real-World Amendment — Reaction Window (Scaling and Recovery Take Time)
#### Description
Detecting trouble early is not enough: creating usable capacity takes time, and the time is not constant.
The system must compare **TTF** against the **measured time-to-act**.
The right question is: “Do we have enough time left to react?”

#### Metrics
- **RT:** `actuation_time_seconds_p95{service}`, `reaction_window_seconds{service}`, `ttf_service_seconds{service}`
- **Audit:** actuation-time histograms/quantiles, breaches where TTF < reaction window

#### Equations
```text
Measured actuation time:
T_act = T_schedule + T_pull + T_init + T_warmup + T_ready + T_endpoints

Reaction window:
T_react = p95(T_act) + safety_margin
reaction_window_seconds = T_react

Rule:
  if TTF_service < T_react:
    - activate backpressure earlier (Article 14)
    - emit ScaleHint immediately if policy allows (Article 19)
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
G = sum_b (slots_b / (P_total_b + ε))

base mode selection:
  normal if (TTF_system >= T_safe) and (stall_sust == 0) and (G >= G_min)
  soft   if (TTF_system <  T_safe) or  (G <  G_min)
  hard   if (TTF_system <  T_hard) or  (stall_sust == 1)

Real-World Amendment — Retry Storm (Positive Feedback Loop):
RSI = retry_rate_per_sec / (success_rate_per_sec + ε)

RSI can override base selection to stop positive feedback early:
  if RSI >= r2: admission_mode = hard
  elif RSI >= r1: admission_mode = soft
  else: admission_mode = (base selection)

optional controlled delay in SOFT mode (to slow retries without hard-failing):
  delay_ms = clamp(d0 * (RSI / (r1 + ε)), 0, d_max)
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

### Real-World Amendment — Capacity Requests (ScaleHint) When Physics Has No Spare Route
#### Description
When there is no alternate internal route (Monopoly Mode) or when TTF is below the reaction window,
continuity cannot come from redistribution alone. Nity must ask for capacity.
This is not “magic automation” — it is a clear, explicit signal that another component may consume (HPA, operator, or human).

#### Metrics
- **RT:** `scale_hint_total{service,reason}`, `scale_hint_active{service}`
- **Audit:** scale-hint counts and outcomes

#### Equations
```text
emit_scale_hint = 1 if (
  (monopoly_mode(service) == 1) OR (TTF_service < T_react)
) else 0

reason examples:
  - monopoly
  - ttf_below_reaction_window
  - systemic_conductance_low
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

**End of Constitution v5.0**
