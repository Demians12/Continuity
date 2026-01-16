# Nity / Continuity - Operational States

This document defines the **concrete state machines** used by Nity.

- States are explicit and enforceable by dataplane/control-plane.
- Regimes (see `regimes.md`) are higher-level postures that *select* state targets.

MVP requires two state machines:
1) Admission state (NORMAL / SOFT / HARD)
2) Failsafe state (NORMAL / HOLD / FALLBACK)

Optional (future / MVP+):
- Backend health state
- Rollout guardian state
- Surge tank state

---

## 1. State machine: Admission

### 1.1 Purpose
Admission is the boundary valve: it controls how many new flows are allowed into the control volume.

### 1.2 States
- NORMAL: admit new flows
- SOFT: admit under a token budget
- HARD: deny new flows (fast fail)

### 1.3 Inputs (MVP)
- TTF_system
- stall_sust (PSI sustained stall)
- conductance proxy G
- optional retry storm index (RSI)

### 1.4 Core equations (plain text)

Conductance proxy:

  G = sum_b ( slots_b / (P_b + eps) )

Mode selection (example):

  if (TTF_system < T_hard) or (stall_sust == 1):
      admission = HARD
  else if (TTF_system < T_safe) or (G < G_min):
      admission = SOFT
  else:
      admission = NORMAL

### 1.5 Soft mode budget (token bucket)

  tokens(t) = min(B, tokens(t-Dt) + r*Dt) - used
  allow      = 1[tokens(t) >= cost]

In SOFT:
- if allow == 1: consume and admit
- else: deny (transient)

### 1.6 Hysteresis (required)

Escalation is immediate.
Relaxation requires stability:

  to move from HARD -> SOFT: require stable for T_recover_min
  to move from SOFT -> NORMAL: require stable for T_recover_min

Also enforce a dwell time:

  no state transition more often than every T_dwell

---

## 2. State machine: Failsafe

### 2.1 Purpose
Failsafe prevents the dataplane from obeying a "dead brain" (stale agent). The dataplane must remain coherent even if the agent fails.

### 2.2 States
- NORMAL: use the projected field (slot tables) normally
- HOLD: freeze the current field (inertia)
- FALLBACK: ignore the field; use deterministic selection within the route group

### 2.3 Inputs (MVP)
- last_agent_seen timestamp (written by agent)
- now (monotonic clock)

### 2.4 Core equations

  age = now - last_agent_seen

  if age < tau1:
      failsafe = NORMAL
  else if age < tau2:
      failsafe = HOLD
  else:
      failsafe = FALLBACK

### 2.5 Behavior per state

NORMAL:
- dataplane uses active slot table + conntrack

HOLD:
- dataplane continues using the currently active table
- ignore flips if possible (belt and suspenders)

FALLBACK:
- select backend deterministically from the equivalent backend set:

  idx     = hash(flow_key) mod N
  backend = fallback_backends[idx]

Important:
- fallback must never escape the route group

---

## 3. Optional state machine: Backend health (MVP+)

This is not required for MVP, but it becomes useful early.

States:
- GREEN: normal
- YELLOW: degraded, reduced slots
- RED: isolate, drip only
- BLACK: fully removed from selection (except explicit diagnostics)

A simple gate can be:
- if err_rate > eta OR latency too high for long enough: YELLOW
- if repeated failures: RED
- if unreachable: BLACK

---

## 4. Event-driven state transitions

Nity prefers event-driven transitions where possible:
- green->yellow->red changes trigger pain signals
- admission escalations trigger audit events

But the actual state machines must be deterministic and based on explicit rules.

---

## 5. MVP checklists

Admission:
- [ ] NORMAL works (no budget checks)
- [ ] SOFT budget works (token bucket)
- [ ] HARD deny is O(1) and consistent
- [ ] hysteresis prevents flapping

Failsafe:
- [ ] heartbeat updates regularly
- [ ] HOLD triggers at tau1
- [ ] FALLBACK triggers at tau2
- [ ] fallback selects only within route group
