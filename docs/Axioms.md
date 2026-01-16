# Nity / Continuity — Hydraulic Constitution v5.0 (Full, 24 Articles)
> **Format:** descriptive + operational (description, metrics, equations)  
> **Scope:** Kubernetes traffic continuity via **eBPF dataplane** + **deterministic control-plane**  
> **Telemetry policy:** the control loop uses **ms–s** signals; **Prometheus/Grafana** are for **auditability** (low-cardinality summaries), not as primary sensors.

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

$$

\tilde{x}(t)=\alpha x(t) + (1-\alpha)\tilde{x}(t-\Delta t)

$$

**Robust derivative (positive consumption only)**

$$

v_x(t)=\max\left(0,\frac{\tilde{x}(t)-\tilde{x}(t-\Delta)}{\Delta}\right)

$$

### Normalization

$$

x^*(t)=\frac{x(t)}{x_{ref}}

$$

### Resource pathology gate (PSI)
For a resource $r \in \{cpu,mem,io\}$ and a window $W$:

$$

stall(W)=\mathbf{1}\left[\mathrm{PSI}^{full}_r(W)>\theta^{full}_r\right] \lor \mathbf{1}\left[\mathrm{PSI}^{some}_r(W)>\theta^{some}_r\right]

$$

Sustained stall over discrete samples:

$$

stall\_sust=\mathbf{1}\left[\sum_{k=1}^{m}stall(t-k\Delta t)\ge m\cdot p\right]

$$

### Minimal backend pressure (MVP-compatible)

$$

P_b = W_Q\cdot Q_b^* + W_L\cdot L_b^* + W_E\cdot E_b^*

$$

where

$$

Q_b^*=\frac{Q_b}{Q_{ref}},\quad L_b^*=\frac{p95\_lat_b}{L_{ref}},\quad E_b^*=\mathrm{clip}\left(\frac{err\_rate_b}{E_{ref}},0,E_{max}\right)

$$

Optional “absolute” error penalty:

$$

\text{if } err\_rate_b>\eta \Rightarrow P_b \leftarrow P_b + K_E

$$

### Slot targets (inverse pressure) and viscous update
Desired weight:

$$

w_b=\frac{\frac{1}{P_b+\epsilon}}{\sum_i \frac{1}{P_i+\epsilon}}

$$

Target slots:

$$

s^{target}_b=\mathrm{round}(w_b\cdot S_{total})

$$

Slew-rate (viscosity):

$$

\Delta s_b = \mathrm{clip}(s^{target}_b-s^{cur}_b,\,-\Delta_{max},+\Delta_{max})

$$

$$
s^{new}_b=s^{cur}_b+\Delta s_b

$$

### Skew and monotonic guard
Observed share:

$$

share_b=\frac{reqs_b}{\sum_i reqs_i+\epsilon}

$$

Skew:

$$

skew=\max_b \left|share_b-w_b\right|

$$

Guard:

$$

\text{if }skew_{t+1}>skew_t+\varepsilon \Rightarrow \Delta_{max}\leftarrow k\Delta_{max}\ \text{or HOLD}

$$

### Conductance proxy and admission modes (backpressure)

$$

G=\sum_b \frac{slots_b}{P_b+\epsilon}

$$

Mode selection (example):
- **normal:** $TTF_{system}\ge T_{safe}$ and $stall\_sust=0$ and $G\ge G_{min}$
- **soft:** $TTF_{system}<T_{safe}$ or $G<G_{min}$
- **hard:** $TTF_{system}<T_{hard}$ or $stall\_sust=1$

---

# Articles 1–24

Each article contains:
- **Description** (what it means + why it exists)
- **Metrics** (Real-time internal vs Audit)
- **Equations** (minimal mathematical grounding)

---

## Article 1 — O(1) Dataplane (Bounded Cost per Event)
### Description
The dataplane must remain **O(1)** per packet/connection event. No per-backend scans, no loops over N backends, no dynamic allocations. This preserves throughput and prevents telemetry from becoming the bottleneck.

### Metrics
- **RT (internal):** `dp_event_cost_ns` (ring-buffer sampled), `dp_map_lookup_fail_total`
- **Audit:** `dp_event_cost_ns_p50/p95/p99`, `dp_map_lookup_fail_total`

### Equations

$$

cost_{event}=t_{end}-t_{start}

$$

Bound invariant:

$$

p99(cost_{event}) \le B_{ns}

$$

---

## Article 2 — Full Physical Key (No Control-Volume Collisions)
### Description
Every control volume is uniquely identified by physical boundary: **VIP + port + protocol (+ slot)**. Multi-port and multi-proto services must not interfere.

### Metrics
- **RT:** `route_keys_active`, `route_key_collision_total`
- **Audit:** collision rate over time

### Equations

$$

route\_key = hash(vip, vport, proto, slot)

$$

$$
collision\_rate=\frac{collisions}{lookups+\epsilon}

$$

---

## Article 3 — Atomic Field Projection by Epoch (A/B Tables + Flip)
### Description
The dataplane must never see a half-updated field. The control-plane writes the inactive table, then flips an **atomic pointer** (`active_table`) and increments **epoch**.

### Metrics
- **RT:** `epoch_current`, `epoch_flip_total`, `agent_heartbeat_age_seconds`
- **Audit:** epoch flip counts, staleness trends

### Equations

$$

staleness = now - last\_heartbeat

$$

Flip allowed only if:

$$

staleness \le \tau_{ok}

$$

---

## Article 4 — Laminar Gear + Real Stickiness (Deterministic Selection + Conntrack)
### Description
Normal operation must not rely on randomness. Initial backend selection uses a deterministic “gear” (cyclic WRR over slots). Once a flow is assigned, it remains bound (stickiness) until an explicit remap reason occurs.

### Metrics
- **RT:** `conntrack_hit_ratio`, `flow_remap_total{reason}`, `slot_pick_total{backend}`
- **Audit:** hit ratio trends, remap reasons distribution

### Equations
Initial pick:

$$

slot = (counter \bmod S)

$$

Stickiness:

$$

hit\_ratio=\frac{hits}{hits+misses+\epsilon}

$$

Remap rate:

$$

remap\_rate=\frac{remaps}{flows\_sampled+\epsilon}

$$

---

## Article 5 — Topological Continuity (Weighted Consistent Mapping Under Churn)
### Description
When backends change, the field must not “reshuffle the universe.” Remaps should be **local and predictable**, minimizing turbulence caused by scaling and rescheduling.

### Metrics
- **RT:** `backend_set_hash`, `table_rebuild_ms`
- **Audit:** `remap_percent_on_churn`, `table_rebuild_ms_p95`

### Equations

$$

remap\_\% = \frac{\#(f: map_t(f)\ne map_{t-1}(f))}{\#flows\_sampled}\cdot 100

$$

Bound (policy):

$$

remap\_\% \le \rho_{max}\ \text{for small churn}

$$

---

## Article 6 — Inertial Fail-Safe (HOLD → FALLBACK)
### Description
The field must not obey a dead brain. If userspace/control-plane stops, the dataplane enters:
1) **HOLD:** freeze the current field  
2) **FALLBACK:** deterministic consistent selection without agent updates

### Metrics
- **RT:** `failsafe_mode{normal,hold,fallback}`, `agent_heartbeat_age_seconds`
- **Audit:** mode time-series

### Equations

$$

age = now - last\_heartbeat

$$

$$
mode=
\begin{cases}
normal,& age<\tau_1\\
hold,& \tau_1 \le age < \tau_2\\
fallback,& age\ge\tau_2
\end{cases}

$$

---

## Article 7 — Layered Pressure (Separate Backend vs Path vs Node)
### Description
“Latency up” is not a single cause. Nity separates pressure into:
- **Backend pressure** (queue, service latency, errors, connection load)
- **Path pressure** (RTT/retrans/jitter)
- **Node pressure** (PSI stall, fd/conntrack exhaustion, IO wait)

### Metrics
- **RT:** `pressure_backend{b}`, `pressure_path{b}`, `pressure_node{n}`
- **Audit:** summaries per service/node

### Equations
Backend (minimal form):

$$

P_{backend}(b)=W_Q Q^*(b)+W_L L^*(b)+W_E E^*(b)+W_C C^*(b)

$$

Node (example):

$$

P_{node}(n)=W_{psi} PSI^*(n) + W_{fd} FD^*(n) + W_{ct} CT^*(n) + W_{io} IO^*(n)

$$

---

## Article 8 — Control Viscosity (Slew-Rate + Hysteresis + Filtering)
### Description
The field must not react to noise. Updates are smoothed by filters, bounded by slew-rate, and protected by hysteresis to prevent flapping.

### Metrics
- **RT:** `slots_delta_per_cycle`, `flap_events_total`, `pressure_ema_alpha`
- **Audit:** `slots_delta_p95`, flap rate per minute

### Equations
Slew:

$$

\Delta s_b=\mathrm{clip}(s^{target}_b-s^{cur}_b,\,-\Delta_{max},+\Delta_{max})

$$

Hysteresis (example):

$$

update\ \text{only if}\ |P_b-P_{b,prev}|>\delta_P\ \text{for}\ T_{hold}

$$

---

## Article 9 — Monotonic Convergence (The Field Must Not Worsen Skew)
### Description
Under normal regime, each control step should **reduce** imbalance (or at least not worsen it). If a step worsens skew, Nity shrinks the step or holds.

### Metrics
- **RT:** `skew_before`, `skew_after`, `monotonic_guard_trigger_total`
- **Audit:** settling time, skew trends

### Equations

$$

skew=\max_b|share_b-w_b|

$$

Guard:

$$

\text{if } skew_{t+1} > skew_t + \varepsilon \Rightarrow \Delta_{max}\leftarrow k\Delta_{max}\ \text{or HOLD}

$$

---

## Article 10 — Selective Permeability (Drip + Cooldown)
### Description
A sick backend should not receive full traffic, but must never become invisible. Nity sends a **controlled diagnostic drip** (token bucket) and applies exponential cooldown on failures.

### Metrics
- **RT:** `drip_tokens{b}`, `drip_allow_total{b}`, `probe_success_ratio{b}`
- **Audit:** drip rate, cooldown durations

### Equations
Token bucket:

$$

tokens_b(t)=\min(B, tokens_b(t-\Delta t)+r\Delta t)-used

$$

Exponential cooldown:

$$

cooldown_{k+1}=\min(C_{max},\gamma\cdot cooldown_k)

$$

---

## Article 11 — Evacuation Principle (Long-Lived Flows Under Degradation)
### Description
A degraded backend must not only stop receiving new traffic; it must reduce its internal pressure. Nity uses staged draining:
1) stop-new + slots→0  
2) dynamic max-connection-age  
3) aggressive shed (last resort; feature-flag)

### Metrics
- **RT:** `active_conns{b}`, `conn_age_p95{b}`, `shed_total{policy}`
- **Audit:** shed events, retry impact

### Equations
Dynamic max-connection-age (example):

$$

MCA = \mathrm{clip}\left(MCA_0 \cdot \frac{TTF}{TTF_{ref}},\ MCA_{min},\ MCA_{max}\right)

$$

---

## Article 12 — Temporal Prediction (TTF)
### Description
Health is not “current usage”; it is **time remaining to loss of control**. TTF estimates how long until a limit is reached at the current consumption velocity.

### Metrics
- **RT:** `ttf_resource_seconds{r}`, `ttf_node_seconds`, `ttf_system_seconds`
- **Audit:** TTF minima and breach events

### Equations

$$

R_r=L_r-U_r,\quad V_r=v_{U_r}(t),\quad TTF_r=\frac{R_r}{\max(\epsilon,V_r)}

$$

$$
TTF_{node}=\min_r(TTF_r),\quad TTF_{system}=\min(TTF_{node},TTF_{backend/system})

$$

---

## Article 13 — Stall Filter (PSI Defines Pathology)
### Description
CPU 100% without stall can be healthy; CPU 100% with stall is pathological. PSI is the operational definition of “the kernel is failing to make progress.”

### Metrics
- **RT:** `psi_cpu_some_10s/60s`, `psi_mem_full_10s/60s`, `stall_sust`
- **Audit:** PSI trend charts, stall breach counts

### Equations
See PSI definitions in the global section:

$$

stall(W)=\mathbf{1}[\mathrm{PSI}^{full}(W)>\theta^{full}] \lor \mathbf{1}[\mathrm{PSI}^{some}(W)>\theta^{some}]

$$

---

## Article 14 — Global Backpressure (The Circuit Breaker)
### Description
System survival outranks external demand. If global conductance drops or TTF goes low, Nity activates backpressure at the boundary:
- **soft:** bounded delay/rate-limit to avoid retry storms  
- **hard:** deny new admissions (e.g., deny connect())

### Metrics
- **RT:** `admission_mode{normal,soft,hard}`, `reject_total{class}`, `delay_injected_ms{class}`
- **Audit:** mode time-series, rejection ratios

### Equations

$$

G=\sum_b \frac{slots_b}{P_b+\epsilon}

$$

Mode selection rule (example):
- normal if $TTF_{system}\ge T_{safe}$, $stall\_sust=0$, $G\ge G_{min}$
- soft if $TTF_{system}<T_{safe}$ or $G<G_{min}$
- hard if $TTF_{system}<T_{hard}$ or $stall\_sust=1$

Retry storm index (optional signal):

$$

RSI=\frac{retries/s}{success/s+\epsilon}

$$

---

## Article 15 — Local Sovereignty (Node Immune System)
### Description
The agent serves its host first. If the local node violates physical limits, it must repel the load balancer by failing readiness before “saving the cluster.”

### Metrics
- **RT:** `local_readiness_state`, `readiness_fail_total{reason}`
- **Audit:** readiness flaps, reason distribution

### Equations

$$

ready=\mathbf{1}[TTF_{node}>T_{min}] \land \mathbf{1}[stall\_sust=0] \land \mathbf{1}[FD^*<\mu] \land \mathbf{1}[CT^*<\nu]

$$

---

## Article 16 — Pain Signaling (Reactive, Event-Driven, Ratelimited)
### Description
Inter-node communication is interrupt-driven: silence is normal. Nodes send “pain signals” only on state transitions (green→yellow→red), using UDP fire-and-forget with authentication and rate limits.

### Metrics
- **RT:** `pain_signal_sent_total`, `pain_signal_drop_total{auth,replay,ratelimit}`
- **Audit:** pain events per node, drop reasons

### Equations

$$

send=\mathbf{1}[state(t)\ne state(t-\Delta t)]

$$

Rate limit via token bucket (same structure as drip).

---

## Article 17 — Fear Factor (Bounded Empathy With Decay)
### Description
When a neighbor falls, a healthy node pre-emptively tightens its own thresholds (“the load will come to me”). This must be bounded to avoid cascades, and must decay back to baseline.

### Metrics
- **RT:** `fear_factor_active`, `fear_delta_threshold_pct`
- **Audit:** cascade cap hits, fear durations

### Equations
Bounded reduction:

$$

\Delta\theta = \min(\theta_{max\_drop}, k\cdot severity)

$$

Decay:

$$

\theta(t)=\theta_0\cdot(1-\Delta\theta\cdot e^{-\frac{t-t_0}{\tau}})

$$

Anti-cascade cap:

$$

\Delta\theta \le \Delta\theta_{cap}\ \text{if many neighbors are red}

$$

---

## Article 18 — Stigmergy (In-Band Telemetry With Staleness Rules)
### Description
State travels attached to traffic (headers/metadata). There is no separate control channel that can desync. In-band telemetry must be versioned and time-bounded.

### Metrics
- **RT:** `inband_parse_fail_total`, `inband_stale_drop_total`
- **Audit:** schema versions seen, drop rates

### Equations

$$

stale=\mathbf{1}[now-ts>TTL]

$$

$$
accept=\mathbf{1}[schema\_ver\in supported]\land \mathbf{1}[stale=0]

$$

---

## Article 19 — Reaction Ladder (Three-Stage Crisis Protocol)
### Description
Response follows a cost hierarchy:
1) **Traffic stress:** scale pods / adjust traffic allocation  
2) **Resource stress:** scale node resources / limits  
3) **Systemic stress:** scale nodes/cluster

Nity chooses stage based on which constraints dominate (TTF vs PSI vs conductance).

### Metrics
- **RT:** `reaction_stage{1,2,3}`, `scaling_action_total{type}`
- **Audit:** action outcomes, stage distribution

### Equations (example rule-set)

$$

stage=
\begin{cases}
1,& TTF_{backend}<T_{scale}\ \land stall\_sust=0\\
2,& stall\_sust=1\ \land (mem/io)\ \text{dominates}\\
3,& G<G_{min}\ \land \text{no local headroom}
\end{cases}

$$

---

## Article 20 — Surge Tank (Bounded Shock Absorber During Scaling)
### Description
During scaling, Nity may temporarily buffer handshakes to avoid hydraulic shock. This buffer must be bounded and integrated with retry-storm protection.

### Metrics
- **RT:** `handshake_buffer_depth`, `handshake_wait_ms_p95`, `handshake_drop_total`
- **Audit:** wait distributions, drops vs RSI

### Equations
Bound:

$$

depth \le D_{max}

$$

Drop rule:

$$

drop=\mathbf{1}[depth\ge D_{max}] \lor \mathbf{1}[RSI>r_{max}]

$$

Wait time:

$$

wait=t_{accepted}-t_{arrived}

$$

---

## Article 21 — Proportional Drainage (Queue Debt Must Be Paid)
### Description
After scaling or recovery, backlog acts as “debt.” Newly available capacity must not be flooded instantly; reported pressure includes debt clearance time.

### Metrics
- **RT:** `queue_debt{b}`, `time_to_clear_debt_seconds{b}`
- **Audit:** debt clearance curves

### Equations

$$

debt=\max(0,Q_{accum})

$$

$$
T_{drain}=\frac{debt}{drain\_rate+\epsilon}

$$

$$
P_{real}=\frac{traffic + debt/T_{drain}}{capacity}

$$

---

## Article 22 — Identity & Security (Physics Cannot Violate Law)
### Description
Traffic may flow only where identity allows. The field is subordinate to security policy (labels/network policies). No “optimization” can bypass authorization.

### Metrics
- **RT:** `policy_denies_total{reason}`, `unauthorized_flow_attempt_total`
- **Audit:** denies by namespace/service, drift detection

### Equations

$$

allow=\mathbf{1}[policy(src,dst,svc)=allow]

$$

---

## Article 23 — Deterministic Simplicity (Every Action Has a Physical Cause)
### Description
Every action must map to a finite set of physical reasons (TTF, PSI stall, error, skew, guard). No black-box decisioning is constitutional.

### Metrics
- **RT:** `decision_reason_total{reason}`, `decision_event_rate`
- **Audit:** reason distribution over time, “unknown reason” must be zero

### Equations (reason selection)
Define normalized scores (examples):

$$

score_{ttf}=\mathbf{1}[TTF_{system}<T_{safe}],\quad score_{psi}=stall\_sust,\quad score_{err}=\mathbf{1}[err\_rate>\eta]

$$

$$
reason(a)=\arg\max_{k\in\{ttf,psi,err,skew,guard\}} score_k

$$

---

## Article 24 — Invariant Harness (Physics CI: Sim + Replay)
### Description
If you cannot test and observe it, you cannot operate it. Nity must ship with a harness that validates invariants (bounded overhead, monotonic skew, no-blackhole, bounded remap, etc.) through simulation and replay.

### Metrics
- **RT:** `invariant_fail_fast_total{test}` (optional)
- **Audit:** `invariant_pass{test}`, `max_remap_pct`, `max_flap_rate`, `dp_cost_p99`

### Equations (examples of invariants)
Bounded dataplane cost:

$$

p99(dp\_event\_cost)\le B_{ns}

$$

Monotonic skew (under stable topology):

$$

skew_{t+1}\le skew_t+\varepsilon

$$

No-blackhole post-admission:

$$

admitted = delivered + explicitly\_rejected

$$

---

## Appendix A — Metric Conventions (Recommended Names)
- RT (in-memory / flight recorder): `rt_*` or structured events (JSON/proto)
- Audit (Prometheus): low-cardinality gauges/counters/histograms
  - `nity_epoch_current`
  - `nity_admission_mode`
  - `nity_ttf_system_seconds`
  - `nity_dp_event_cost_ns_bucket` (histogram)
  - `nity_skew`
  - `nity_guard_hits_total`
  - `nity_reject_total{class}` (keep class cardinality bounded)

---

## Appendix B — Defaults (Good Starting Points)
> These are starting points, not truth. They exist to make the system immediately operable.

- Δt: 200ms
- Δ (robust derivative window): 5s
- Δmax (slots/cycle): 1–3 (per backend)
- τ1 (HOLD): 2–5s
- τ2 (FALLBACK): 10–20s
- PSI windows: 10s + 60s
- $T_{safe}$: 60–180s, $T_{hard}$: 10–30s (depends on scale reaction time)

---

**End of Constitution v5.0**