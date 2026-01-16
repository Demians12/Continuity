# Nity / Continuity - Operating Regimes

This document defines **regimes** as the system's high-level *posture* (how aggressively it must protect continuity).

- **Regimes** are *macro* behaviors: a stable interpretation of risk and intent.
- **States** are *concrete toggles* and modes used by dataplane/control-plane (see `states.md`).

A useful mental model:
- Regime = "what kind of situation are we in?"
- State  = "what switches are currently ON?"

Nity can be implemented with states only, but regimes prevent ambiguity and help keep transitions predictable.

---

## 1. The four regimes

### 1.1 Laminar (normal)
**Intent:** maintain high predictability and low oscillation.

**Typical conditions**
- TTF_system is comfortably above safety threshold.
- No sustained PSI stall.
- Pressure is stable or improving.
- Conductance proxy (G) is healthy.

**Allowed actions**
- Small slot corrections (low slew-rate).
- Prefer stickiness (conntrack) and stable weights.
- Drip only for diagnostics (very small).

**Typical state mapping**
- admission_mode = NORMAL
- failsafe_mode  = NORMAL

---

### 1.2 Transition (degradation)
**Intent:** isolate risk early, before a crisis forms.

**Typical conditions**
- TTF_system trending down OR pressure trending up.
- Errors increase or latency increases persistently.
- PSI stalls may appear intermittently.

**Allowed actions**
- Stronger slot rebalancing (higher but still bounded slew).
- Activate selective isolation (drip/cooldown) for unhealthy backends.
- Soft admission gating (budget) may be enabled.

**Typical state mapping**
- admission_mode = NORMAL or SOFT
- failsafe_mode  = NORMAL

---

### 1.3 Crisis (survival)
**Intent:** protect the system even if it rejects new work.

**Typical conditions**
- TTF_system below hard threshold OR sustained PSI stall.
- Conductance falls below minimum viable.
- Rapid error escalation (leak) suggests a failing backend.

**Allowed actions**
- Hard admission deny for new connections.
- Aggressive isolation of failing backends.
- Prefer stability (HOLD) over frequent re-projections if signals are chaotic.

**Typical state mapping**
- admission_mode = HARD
- failsafe_mode  = NORMAL or HOLD (if the agent is unstable)

---

### 1.4 Recovery
**Intent:** return to normal without overshooting (avoid bounce-back instability).

**Typical conditions**
- The crisis trigger is no longer present, but system is still "hot".
- Queue/backlog debt exists.
- Pressure is decreasing but not yet stable.

**Allowed actions**
- Slow reopening (hysteresis) to avoid immediate relapse.
- Continue paying queue debt; gradually re-enable capacity.
- Decrease throttling step-by-step.

**Typical state mapping**
- admission_mode = SOFT then NORMAL (timed)
- failsafe_mode  = NORMAL

---

## 2. Entry/exit conditions (explicit)

Nity should never transition regimes based on a single sample.
Use:
- filtering (EWMA),
- time windows,
- and hysteresis timers.

Recommended signals (MVP):
- TTF_system
- stall_sust (from PSI)
- conductance proxy G
- error gate (err_rate > eta)

### 2.1 Core definitions (plain text)

Robust derivative (positive consumption):

  v_x(t) = max(0, (ewma(x,t) - ewma(x,t-D)) / D )

TTF per resource r:

  R_r   = limit_r - usage_r
  V_r   = v_usage_r(t)
  TTF_r = R_r / max(eps, V_r)

TTF_system (MVP):

  TTF_node   = min_r(TTF_r)
  TTF_system = min_over_nodes(TTF_node)

Conductance proxy:

  G = sum_b ( slots_b / (P_b + eps) )

Sustained stall:

  stall_sust = 1[ sum_{k=1..m} stall(t-k*Dt) >= m*p ]


### 2.2 Suggested thresholds
You tune these later. Provide defaults to remove ambiguity:

- T_safe:  60-180s
- T_hard:  10-30s
- G_min:   service-dependent (calibrate)
- Recovery minimum time (T_recover_min): 10-60s

---

## 3. Regime-to-state mapping

Regimes do not directly manipulate packets. They map to explicit states:

- Admission state: NORMAL / SOFT / HARD
- Failsafe state: NORMAL / HOLD / FALLBACK
- Backend health state (optional in MVP): GREEN / YELLOW / RED / BLACK

Example mapping table:

| Regime      | Admission | Failsafe | Notes |
|-------------|-----------|----------|------|
| Laminar     | NORMAL    | NORMAL   | minimal changes |
| Transition  | SOFT*     | NORMAL   | *only if needed |
| Crisis      | HARD      | NORMAL/HOLD | survival first |
| Recovery    | SOFT -> NORMAL | NORMAL | hysteresis required |

---

## 4. Anti-flapping rules

To keep behavior predictable:

1) Escalation is fast; relaxation is slow.
   - entering crisis can be immediate when hard triggers fire
   - leaving crisis requires stability for T_recover_min

2) Regime changes require a minimum dwell time:

  if regime changes at t0, do not change again until t0 + T_dwell

3) Avoid "double-trigger" loops:
   - do not simultaneously increase admission and aggressively re-open slots

---

## 5. MVP implementation guidance

In the MVP, you can implement regimes as a thin layer that computes:
- `desired_admission_mode`
- `desired_slew_budget` (Delta_max)
- `desired_drip_policy` (rates/cooldowns)

Everything else is handled by the concrete state machines defined in `states.md`.
