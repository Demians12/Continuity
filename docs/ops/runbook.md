# Nity Runbook (MVP)

This is a minimal operational guide for early users.

---

## 1. What Nity does (in one sentence)

Nity keeps traffic flowing by shaping **new admissions** and redistributing **new flows** across equivalent backends using a deterministic Eulerian field.

---

## 2. What Nity does NOT do (MVP)

- It does not route traffic between different services.
- It does not create spare replicas by itself (unless an operator is added).
- It does not perform per-request scheduling at L7.

---

## 3. Reading the two key modes

### 3.1 Admission mode
- NORMAL: healthy, no gating
- SOFT: budget-gated admission (throttled)
- HARD: fast-fail new admissions

If you see SOFT/HARD, ask:
- Is TTF_system low?
- Is PSI stall sustained?
- Is conductance G low?

### 3.2 Failsafe mode
- NORMAL: agent is fresh
- HOLD: agent is stale; field is frozen
- FALLBACK: agent is very stale; deterministic selection is used

If you see HOLD/FALLBACK, check:
- agent health
- node pressure
- restart loops

---

## 4. First-response playbook

If the system enters CRISIS (regime) and HARD admission:
1) Confirm whether the route group has more than one backend.
2) If only one backend exists, the correct action is scaling or reducing incoming load.
3) If multiple backends exist, check which backend is leaking (errors) or stalling.
4) Expect Nity to reduce slots on the unhealthy backend and protect capacity.

---

## 5. What to tune first

- Delta_max (viscosity): too high causes oscillation, too low causes slow reaction.
- T_safe / T_hard: should reflect how fast you can realistically recover capacity.
- Pressure weights: W_E should dominate when errors appear.

---

## 6. Auditability metrics (Prometheus)

Keep low-cardinality:
- nity_admission_mode
- nity_failsafe_mode
- nity_ttf_system_seconds
- nity_epoch_current
- nity_guard_hits_total

Use flight-recorder output for high-resolution debugging.

