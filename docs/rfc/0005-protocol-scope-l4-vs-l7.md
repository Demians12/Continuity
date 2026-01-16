# RFC 0005 — Protocol Scope: L4 Continuity vs L7 Semantics

- **Status:** Draft (MVP+)
- **Owner:** Nity / Continuity
- **Scope:** Clarify what Nity guarantees at L4, and what is out of scope without L7 signals

---

## 1. Why this RFC exists

Many misunderstandings come from mixing:
- **connection-level** continuity (L4)
with
- **request-level** semantics (L7).

Nity’s MVP operates at **L4 connect admission + backend selection**. This already provides strong continuity properties, but it does not magically solve every L7 scenario.

This RFC makes those boundaries explicit to prevent “implicit promises”.

---

## 2. Goals

1) State the MVP guarantee precisely: continuity for **new connections** and stable backend binding for **flows**.
2) Explain the limits for HTTP/2, gRPC, and long-lived multiplexed streams.
3) Define safe extension points for L7 signals without violating Axiom Zero (no per-request decision layer).

---

## 3. MVP guarantee (L4)

MVP guarantees (per route group):
- O(1) dataplane selection per connect event
- deterministic selection + stickiness (conntrack)
- coherent field projection (epoch flip)
- admission control (normal/soft/hard)
- fail-safe continuity (hold/fallback)

What that means operationally:
- You can shape the **rate of new connections**
- You can redirect **new flows** to healthier backends
- You can isolate broken backends without blackholing everything

---

## 4. L7 limitations (what MVP does not claim)

### 4.1 HTTP/2 and gRPC multiplexing
A single TCP connection can carry many logical requests. If Nity binds a connection to a backend, it does not re-route requests inside that connection.

Implication:
- continuity at L4 can still prevent new bad connections,
- but does not “move” active L7 streams.

### 4.2 Application-level queues
If the app maintains internal queues, Nity may only see symptoms (latency, errors) unless an app signal is provided.

MVP approach:
- use robust pressure + stall/TTF for prevention,
- optional app signals are allowed but must remain low-cardinality and field-oriented.

---

## 5. Extension points (safe and constitutional)

Allowed extensions (examples):
- low-cardinality service-level latency/err signals (p95/p99 aggregates)
- queue depth proxies (bounded)
- per-backend “health class” hints from a sidecar
- in-band telemetry headers (stigmergy) only if versioned and bounded (later)

Not allowed:
- per-request decisioning in the agent
- building a full per-user policy engine in the control plane

---

## 6. Acceptance criteria

- README and docs must not claim L7 per-request scheduling in MVP.
- Any L7 signal must be:
  - bounded in overhead,
  - bounded in cardinality,
  - explainable as a field input (Eulerian), not a particle tracker.

---

## 7. References

- Constitution: Axiom Zero, Deterministic Simplicity
- Control loop: RFC 0001
- Dataplane contract: RFC 0004
