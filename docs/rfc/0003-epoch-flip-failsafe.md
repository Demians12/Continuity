# RFC 0003 — Atomic Epoch Flip and Failsafe (HOLD → FALLBACK)

- **Status:** Draft (MVP)
- **Owner:** Nity / Continuity
- **Scope:** Dataplane safety mechanisms: coherent field projection, agent staleness handling

---

## 1. Context

The dataplane must remain coherent even when:
- the agent is slow,
- the agent crashes,
- the node is under pressure,
- partial updates would otherwise occur.

Nity achieves this with:
1) **A/B slot tables** and an **atomic flip** (`active_table`, `epoch`)
2) **Failsafe modes** driven by agent heartbeat staleness:
   - HOLD: freeze field (inertia)
   - FALLBACK: deterministic selection without agent updates

---

## 2. Goals (MVP)

1) Dataplane never reads a partially updated slot table.
2) Field transitions are atomic and observable (epoch increments).
3) If the agent is stale, the dataplane continues in a coherent mode (hold/fallback).
4) Fallback selection stays deterministic and **per route group** (no global mixing).

---

## 3. Non-goals (MVP)

- Distributed consensus, leader election, quorum control.
- Cross-node coordination (gossip) for failsafe (local-only in MVP).
- L7 fallback routing.

---

## 4. Data structures (overview)

Exact map layouts are defined in RFC 0004. Conceptually:
- `slot_table_A[route_key] -> backend_id`
- `slot_table_B[route_key] -> backend_id`
- `active_table` indicates which table is active (A or B)
- `epoch` increments on each flip
- `last_agent_seen_ts` updated periodically by agent
- `failsafe_mode` derived in dataplane from `age = now - last_agent_seen_ts`

---

## 5. Atomic projection (A/B flip)

### 5.1 Write inactive, flip active
Agent algorithm (conceptual):
1) determine inactive table (not `active_table`)
2) write complete slot mapping into inactive table
3) atomically set `active_table = inactive`
4) increment `epoch`

Dataplane algorithm:
- read `active_table` once
- use only that table for the decision path

### 5.2 Invariants
```text
Dataplane must never observe a mix of A and B within one decision.
Epoch changes must correspond to complete table projections.
```

---

## 6. Failsafe: staleness → HOLD → FALLBACK

### 6.1 Staleness measurement
```text
age = now - last_agent_seen_ts
```

### 6.2 Thresholds
- `τ1`: HOLD threshold (freeze field)
- `τ2`: FALLBACK threshold (stop trusting field updates)

### 6.3 State definition
```text
failsafe_mode =
  normal   if age <  τ1
  hold     if τ1 <= age < τ2
  fallback if age >= τ2
```

### 6.4 Behavior per mode

#### normal
- Use `active_table` + slot selection as usual.
- Allow epoch flips (agent-controlled).

#### hold
- Continue using the currently active table.
- Ignore any partial/late agent behavior by refusing flips (dataplane policy).
- Conntrack continues normally (stickiness preserved).

Implementation note:
- Safest is “belt and suspenders”: agent stops flipping when unhealthy and dataplane also derives hold from staleness.

#### fallback
- Ignore slot tables and epoch.
- Select backend deterministically from the **equivalent backend set** for the route group.

Fallback must be per route group:
```text
idx     = hash(flow_key) mod N
backend = fallback_backends[idx]
```

---

## 7. Flow keys, route groups, and correctness

- **Route group** determines which backend set is valid.
- **Flow key** provides determinism per flow (consistent behavior even in fallback).

Fallback must never “escape” the route group. No cross-service routing.

---

## 8. Metrics (RT vs audit)

RT:
- `failsafe_mode`
- `agent_heartbeat_age`
- `epoch_seen`
- `fallback_used_total`

Audit:
- `nity_failsafe_mode` (gauge)
- `nity_agent_heartbeat_age_seconds` (gauge)
- `nity_epoch_current` (gauge)
- `nity_fallback_used_total` (counter)

---

## 9. Acceptance criteria (MVP)

1) No partial table exposure under high churn or concurrent updates.
2) HOLD triggers correctly when agent heartbeat is stale.
3) FALLBACK triggers correctly and stays deterministic per route group.
4) Conntrack behavior remains coherent across normal/hold transitions.

---

## 10. Open questions

1) Should dataplane enforce “ignore epoch flips” in hold, or should agent avoid flipping? (Both is safest.)
2) How do we store fallback backend sets efficiently per route group? (See RFC 0004 options.)
3) What is the minimal flow_key that is stable and verifier-friendly?

---

## 11. References

- Control loop: RFC 0001
- Backpressure: RFC 0002
- Dataplane contract: RFC 0004
- States guide: `docs/guide/states.md`
