# Nity / Continuity — Equations (Canonical, GitHub-Friendly)

**Purpose.** This document is the *canonical* reference for:
- symbols, units, and normalization rules
- equations used by the control loop and dataplane knobs
- preconditions and fallbacks (what to do when a signal is missing)
- mapping from equations to implementation modules

**Formatting policy.** All math is written in **plain text** (ASCII) so it renders cleanly on GitHub and in terminals.

**Scope.** MVP + forward-compatible expansions. If an equation exists both here and in the Constitution, this file is the “engineer’s exact spec” (units, clipping, gating, and defaults).

---

## 0) Conventions

### 0.1 Units (do not mix)
- time: `ms`, `s`
- rates: `1/s`, `req/s`, `err/s`
- ratios: `0..1`
- memory: `bytes`
- CPU: `cores` or `millicores` (choose one and keep it consistent)
- latency: `ms`
- PSI: ratio `0..1` (kernel reports microseconds; convert consistently)

### 0.2 Symbols (global)
- `b` = backend index within an **equivalent backend set**
- `g` = route group / control volume (VIP,port,proto)
- `t` = time
- `Δt` = control tick period (e.g., 200ms)
- `Δ` = robust derivative window (e.g., 5s)
- `ε` = small constant to prevent division by zero (e.g., 1e-9)
- `clip(x, lo, hi)` = clamp x into [lo, hi]

### 0.3 "Equivalent backend set" rule (non-negotiable)
All routing/slot allocation is performed **only within the backend set that can serve the same route group**.
If `|B_g| == 1` (monopoly):
- you cannot “reroute” to other services
- continuity comes from **admission control** + **scale hints** (optional)

### 0.4 RT vs Audit signals
- **RT (real-time)**: used directly by the loop (ms–s scale). Often lives in-memory / flight recorder.
- **Audit**: low-cardinality Prometheus summaries for humans and postmortems.

---

## 1) Signal conditioning (filters and derivatives)

### 1.1 EWMA (Exponential Weighted Moving Average)
Use EWMA to reduce noise without introducing heavy lag.

Equation:
```text
ewma(x,t) = α*x(t) + (1-α)*ewma(x,t-Δt)
```
- `α` in [0.05..0.3] depending on Δt and desired smoothness.
- Larger α = reacts faster, less smoothing.

### 1.2 Robust derivative (positive-only)
Used for “consumption velocity” (TTF) and trend detection.
```text
v_pos(x,t) = max(0, (ewma(x,t) - ewma(x,t-Δ)) / Δ )
```
- Use `Δ` >= 2s (typically 5s) to avoid reacting to short spikes.
- If your signal is “remaining” rather than “usage,” flip sign accordingly.

### 1.3 Normalization
Normalization keeps weights portable across workloads.
```text
norm(x,t) = x(t) / x_ref
```
- Always document `x_ref` for each normalized term.
- If `x_ref` is dynamic, define how it is computed (e.g., rolling median).

---

## 2) Backend pressure (P_b) — canonical MVP form

**Goal:** compress multiple “resistances” into a single scalar pressure used for routing decisions.

### 2.1 Inputs (preferred MVP)
- `Q_b` = queue proxy for backend b (unit: requests or connections)
- `L_b` = p95 service latency for backend b (ms)
- `E_b` = error rate for backend b (ratio 0..1)

If `Q_b` is unavailable, set `W_Q=0` and rely on latency/error + node stall gating.

### 2.2 Normalized terms
```text
Q*_b = Q_b / Q_ref
L*_b = L_b / L_ref
E*_b = clip(E_b / E_ref, 0, E_max)
```
Recommended references (starting points):
- `Q_ref`: typical healthy queue proxy (or use a constant like 50)
- `L_ref`: SLO-ish latency (e.g., 50ms or 100ms)
- `E_ref`: tolerated error (e.g., 0.01 for 1%)
- `E_max`: cap to prevent one backend from dominating the sum (e.g., 20)

### 2.3 Pressure equation (MVP)
```text
P_b = W_Q*Q*_b + W_L*L*_b + W_E*E*_b
```
**Absolute error penalty (recommended):**
```text
if E_b > η_err_abs:
    P_b = P_b + K_E
```
Where:
- `η_err_abs` might be 0.05 (5%) or lower depending on service criticality.
- `K_E` is a large constant (e.g., 5..20) to force pressure dominance by leakage.

### 2.4 Preconditions / fallbacks
- If latency signal is missing: set `W_L=0` and log a “signal_missing.latency” reason.
- If errors are missing: set `W_E=0` but keep stall gating (PSI) active.
- If both missing: the loop should NOT pretend it is doing smart routing; it should move towards conservative admission and/or require instrumentation.

---

## 3) Pressure layering (backend vs path vs node)

The Constitution separates sources of resistance. You can compute a layered pressure vector and combine it (weights optional).

### 3.1 Backend pressure (service-local)
Use section 2 as canonical:
```text
P_backend(b) = P_b
```

### 3.2 Path pressure (network symptoms)
Optional (MVP can omit):
- `rtt_ms`, `retrans_rate`, `jitter_ms`
```text
P_path(b) = W_rtt*norm(rtt_ms, RTT_ref)
          + W_retx*norm(retrans_rate, RETX_ref)
          + W_jit*norm(jitter_ms, JIT_ref)
```

### 3.3 Node pressure (kernel pathology)
Use PSI as the primary “pathology gate.” (See section 6.)
You may convert stall into an additive pressure:
```text
P_node(n) = W_psi * stall_score(n)
          + W_fd  * norm(fd_used/fd_limit, FD_ref)
          + W_ct  * norm(ct_used/ct_limit, CT_ref)
          + W_io  * norm(iowait_ratio, IO_ref)
```
For MVP, it is acceptable to use:
```text
P_node(n) = W_psi * stall_score(n)
```
and keep the other terms as future expansions.

### 3.4 Combined pressure (optional)
If you do combine layers, keep it explicit:
```text
P_total(b) = P_backend(b) + P_path(b) + P_node(node_of_b)
```
- Do not hide this behind “magic.” Each term must have a reason label.

---

## 4) Converting pressure to desired routing weights

Nity uses an “inverse resistance” law: lower pressure attracts more flow.

### 4.1 Inverse-pressure weight
```text
inv_b = 1 / (P_b + ε)
w_b   = inv_b / sum_i inv_i
```
- `w_b` is the desired share in [0..1].
- If all backends are equally pressured, weights approach uniform.

### 4.2 Hard isolation
If a backend is isolated (health state == isolated), force:
```text
w_b = 0
```
Then renormalize remaining weights. If all weights become 0, system must enter admission protection (soft/hard) rather than “routing to nowhere.”

---

## 5) Slots as conductance (capacity allocation)

Slots represent the discrete capacity “openings” assigned to each backend.

### 5.1 Target slots from weights
```text
s_target_b = round( w_b * S_total )
```
Post-processing rule (required):
- Ensure `sum_b s_target_b == S_total` by distributing rounding error deterministically.

### 5.2 Viscous update (slew-rate / damper)
Slots cannot jump arbitrarily; otherwise you get oscillation.
```text
Δs_b  = clip(s_target_b - s_cur_b, -Δmax, +Δmax)
s_new_b = s_cur_b + Δs_b
```
- `Δmax` is per-backend per-tick (e.g., 1..3).
- In **Crisis**, you may increase Δmax (but do it explicitly as a regime rule).

### 5.3 Hysteresis (anti-flap)
Do not update on tiny changes:
```text
update_allowed = (abs(P_b - P_prev_b) > δP) sustained for T_hold
```
or equivalently:
```text
update_allowed = (abs(w_b - w_prev_b) > δw) sustained for T_hold
```

---

## 6) PSI stall gate (pathology definition)

PSI (Pressure Stall Information) defines when “100% CPU” is fine vs pathological.

### 6.1 Stall predicate
For resource r in {cpu, mem, io} and window W in {10s, 60s}:
```text
stall(W) = 1[ PSI_full_r(W) > θ_full_r ] OR 1[ PSI_some_r(W) > θ_some_r ]
```
### 6.2 Sustained stall (discrete samples)
```text
stall_sust = 1[ sum_{k=1..m} stall(t-k*Δt) >= m*p ]
```
- example: `m=10`, `p=0.7` means 7 out of last 10 samples show stall.

### 6.3 Stall score (optional)
Instead of boolean, you can define a bounded score:
```text
stall_score = clip( PSI_full_cpu(10s)/θ_full_cpu, 0, S_max )
```
For MVP, boolean is fine and easier to reason about.

---

## 7) TTF (Time To Failure) — canonical form

TTF estimates the time remaining until loss of control.

### 7.1 Resource-level TTF
For each resource r:
- `L_r` = hard limit (bytes, cores, fds, conntrack entries, etc.)
- `U_r(t)` = current usage
- `R_r(t)` = remaining capacity = `L_r - U_r(t)`
- `V_r(t)` = consumption velocity = `v_pos(U_r,t)`

```text
R_r(t)   = L_r - U_r(t)
V_r(t)   = v_pos(U_r,t)
TTF_r(t) = R_r(t) / max(ε, V_r(t))
```

### 7.2 Node-level TTF
```text
TTF_node(t) = min_r TTF_r(t)
```

### 7.3 System-level TTF (simple MVP)
```text
TTF_system(t) = min_over_nodes TTF_node(t)
```
You may also compute `TTF_service` and take the minimum across relevant scopes.

### 7.4 Preconditions and corrections
- If `V_r ~ 0`, then `TTF_r` becomes very large; treat it as “stable for now.”
- If `R_r < 0`, clamp to 0 and immediately enter protection.

---

## 8) Skew (load divergence) and monotonic guard

Skew measures how far actual distribution is from desired weights.

### 8.1 Observed share
```text
share_b = reqs_b / (sum_i reqs_i + ε)
```

### 8.2 Skew
```text
skew = max_b abs( share_b - w_b )
```

### 8.3 Monotonic guard
If a step makes skew worse, reduce step or HOLD.
```text
if skew_next > skew_now + ε_skew:
    Δmax = k * Δmax     (0 < k < 1)
    OR HOLD for this tick
```
- In stable topology, repeated worsening indicates the controller is too aggressive or a signal is lying.

---

## 9) Conductance proxy (G) and admission mode selection

Conductance is a coarse proxy for “how easily flow can move.”

### 9.1 Conductance proxy
```text
G = sum_b ( slots_b / (P_b + ε) )
```
- Higher G means more “open capacity per resistance.”
- Use only as a thresholding signal, not as a target itself (MVP).

### 9.2 Admission mode policy (example)
Given thresholds `T_safe`, `T_hard`, and `G_min`:
```text
normal if (TTF_system >= T_safe) AND (stall_sust == 0) AND (G >= G_min)
soft   if (TTF_system <  T_safe) OR  (G <  G_min)
hard   if (TTF_system <  T_hard) OR  (stall_sust == 1)
```
Hysteresis rule (recommended):
- Escalation (normal->soft->hard) can be immediate.
- De-escalation requires stability for `T_recover_min`.

---

## 10) Soft admission budget (token bucket)

Soft mode is “admit under a bounded budget.”

### 10.1 Token dynamics
```text
tokens(t) = min(B, tokens(t-Δt) + r*Δt) - used
allow      = 1[ tokens(t) >= cost ]
```
- typical `cost = 1` per new admission
- `B` = burst capacity
- `r` = refill rate (tokens/s)

### 10.2 Deny decision in soft mode
```text
if allow == 1:
    used = cost
    admit
else:
    deny (transient)
```

---

## 11) Failsafe modes (agent staleness)

Failsafe is derived from heartbeat age.

### 11.1 Heartbeat age
```text
age = now - last_agent_seen_ts
```

### 11.2 Mode selection
```text
normal   if age <  τ1
hold     if τ1 <= age < τ2
fallback if age >= τ2
```

### 11.3 Fallback selection (within route group only)
```text
idx     = hash(flow_key) mod N_backends_in_group
backend = backends[idx]
```

---

## 12) Backlog debt (queue repayment) — optional but important

After scaling or recovery, backlog behaves like debt and must be paid gradually.

### 12.1 Debt
```text
debt_b = max(0, Q_accum_b)
```

### 12.2 Time to drain
```text
T_drain_b = debt_b / max(ε, drain_rate_b)
```

### 12.3 Debt-adjusted pressure (used together with baseline P_b)
Use this only if you have a meaningful queue proxy and drain estimate.
```text
P_debt(b) = debt_b / max(ε, T_drain_b)
P_total(b) = P_b + W_debt * norm(P_debt(b), Debt_ref)
```
Note: this complements the baseline pressure; do not duplicate error penalties.

---

## 13) Decision reasoning (deterministic simplicity)

Every action must have an explicit reason label.

### 13.1 Reason scoring (example)
```text
score_ttf  = 1[TTF_system < T_safe]
score_psi  = stall_sust
score_err  = 1[E_b > η_err_abs]
score_skew = 1[skew > skew_max]
reason     = argmax(score_*)
```
Implementation should also store the “top 2 reasons” for debugging.

---

## 14) Implementation mapping (recommended files)

These are suggested module boundaries (adapt to your repo layout):

- EWMA / derivative:
  - `internal/agent/util/ewma.go`
  - `internal/agent/util/robust_derivative.go`
- Pressure:
  - `internal/agent/controller/pressure.go`
- Slots + viscosity + hysteresis:
  - `internal/agent/controller/slots.go`
- Skew + monotonic guard:
  - `internal/agent/controller/guard.go`
- Admission mode selection + token bucket:
  - `internal/agent/controller/admission.go`
- Failsafe thresholds (agent side) + heartbeat update:
  - `internal/agent/controller/failsafe.go`
- PSI + cgroup sensors:
  - `internal/agent/sensors/psi_linux.go`
  - `internal/agent/sensors/cgroup_linux.go`

---

## 15) Default parameters (starting points, not truth)

- `Δt` = 200ms
- `Δ`  = 5s
- `Δmax` = 1..3 slots/tick/backend
- `τ1` = 2..5s, `τ2` = 10..20s
- `T_safe` = 60..180s, `T_hard` = 10..30s
- Pressure weights:
  - start with `W_L=1.0`, `W_E=2.0..5.0`, `W_Q=0..1.0` depending on availability
- Error constants:
  - `η_err_abs` = 0.02..0.05
  - `K_E` = 5..20

---

## 16) What MUST be explicit in code reviews

- units for every metric (ms vs s, bytes vs MiB)
- normalization references (`*_ref`)
- clipping bounds
- missing-signal behavior (do not silently “guess”)
- reason labeling on every decision that changes state/slots/mode

---

**End of equations spec.**
