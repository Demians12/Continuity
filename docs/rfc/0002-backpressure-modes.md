# RFC 0002 — Backpressure Modes (Admission Control): Normal / Soft / Hard

- **Status:** Draft (MVP)
- **Owner:** Nity / Continuity
- **Scope:** Admission control semantics enforced by dataplane, configured by agent

---

## 1. Context

In Kubernetes, continuity often fails due to **retry storms**, **queue explosions**, and **kernel stall cascades**. Nity treats admission control as a hydraulic valve: if system conductance drops or time-to-failure becomes short, the boundary must reduce inflow to preserve the flows already inside.

Backpressure is not “giving up”; it is the physically correct response to insufficient capacity.

---

## 2. Goals (MVP)

1) Provide explicit, deterministic admission modes:
   - `normal`: admit
   - `soft`: admit under a budget (throttled admission)
   - `hard`: fast-fail new admissions
2) Ensure modes do not flap via hysteresis (implemented by agent; described here as contract).
3) Ensure backpressure does not amplify retry storms (client-visible behavior must be predictable).

---

## 3. Non-goals (MVP)

- Implementing real “sleep/delay” in `sockops/connect4` (eBPF cannot block there).
- Full L7 rate limiting and per-user policies.
- Complex multi-class QoS (can be layered later; keep MVP bounded).

---

## 4. Definitions

- **Admission:** whether a new flow/connection is allowed into the control volume.
- **Boundary:** the earliest enforcement point available for the chosen dataplane hook.
- **Budget:** a token-based allowance that bounds how many new admissions are permitted per time.

---

## 5. Modes (contract)

### 5.1 Mode: normal
- Admit new flows.
- Dataplane selection proceeds with conntrack hit/miss and slot table selection.

### 5.2 Mode: soft
Soft admission must reduce inflow without violating dataplane constraints.

MVP contract:
- Soft admission is **budget-gated**:
  - if tokens available: allow
  - else: reject with a **transient** failure signal
- Optional shaping/delay is allowed only at hooks where it is feasible (e.g., TC/XDP), not by sleeping in sockops.

### 5.3 Mode: hard
- Fast-fail new admissions (deny connect).
- Must be low-overhead, deterministic, and avoid expensive work.

---

## 6. Budgeting model (token bucket)

Per route group (and optionally per class later):
```text
tokens(t) = min(B, tokens(t-Δt) + r*Δt) - used
allow = 1[tokens(t) >= cost]   where cost is typically 1 per admission
```

Parameters:
- `B`: burst capacity
- `r`: refill rate (tokens/sec)

In soft mode:
- if `allow == 1`: consume and admit
- else: reject (transient)

---

## 7. Which signals trigger which mode?

The agent decides mode based on core survival signals:
- `TTF_system` (time to loss of control)
- `stall_sust` (PSI sustained pathology)
- `G` (conductance proxy)
- optional: `RSI` (retry storm index)

Recommended rule set (example):
```text
Normal: (TTF_system >= T_safe) AND (stall_sust == 0) AND (G >= G_min)
Soft:   (TTF_system <  T_safe) OR  (G <  G_min)
Hard:   (TTF_system <  T_hard) OR  (stall_sust == 1)
```

Hysteresis:
- Stricter transitions are immediate.
- Relaxing requires stability for `T_recover_min` (see regimes guide).

---

## 8. Client-visible behavior (avoid storm amplification)

Backpressure is only useful if it does not create feedback loops.

### 8.1 Hard mode errors
Hard mode should fail fast. Prefer a stable, recognizable failure:
- `ECONNREFUSED` or `EHOSTUNREACH` or a consistent “deny connect” policy.

Pick one and keep it consistent to avoid ambiguous client behavior.

### 8.2 Soft mode errors
Soft mode should encourage clients to back off:
- return a transient failure that typical clients can retry with jitter/backoff.

Nity cannot force client behavior, but it can avoid making storms worse by being consistent and bounded.

### 8.3 Retry storm index (optional signal)
```text
RSI = retries_per_s / (success_per_s + ε)
```
If RSI is high, prefer moving to hard mode sooner (policy choice).

---

## 9. Dataplane enforcement points

MVP typically enforces admission at L4 connect (`sockops/connect4`).

Notes:
- “Delay injection” is not possible by sleeping in sockops.
- If you want shaping, implement it at TC/XDP (future extension) or as an external gate.

---

## 10. Metrics (RT vs audit)

RT (agent-local / flight recorder):
- `admission_mode`
- `tokens_remaining`
- `admissions_allowed_total`
- `admissions_denied_total{mode}`
- `rsi` (if implemented)

Audit (Prometheus):
- `nity_admission_mode` (gauge)
- `nity_admissions_denied_total{mode}` (counter)
- `nity_tokens_remaining` (gauge, low-cardinality)

---

## 11. Acceptance criteria (MVP)

- Hard mode denies are O(1) and bounded cost.
- Soft mode gating is bounded and does not require heavy per-flow state.
- Modes do not flap under noisy signals due to hysteresis.
- No-blackhole: admissions are either delivered or explicitly denied.

---

## 12. Open questions

1) Which exact error code should hard deny use for best operational behavior?
2) Do we want soft mode as “transient reject only” in MVP, and add shaping later?
3) Should budgets be per service only (MVP) or per “class” early?

---

## 13. References

- Control loop: RFC 0001
- Epoch flip and failsafe: RFC 0003
- Dataplane contract: RFC 0004
- Regimes/states guides: `docs/guide/regimes.md`, `docs/guide/states.md`
