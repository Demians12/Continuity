# Continuity
**Deterministic traffic continuity for Kubernetes, inspired by Eulerian fluid mechanics.**

Nity treats service traffic as **fluid** and backends as fixed points on a mesh. Instead of tracking individual requests in the decision layer (Lagrangian view), it controls **pressure fields** and **flow capacity** at **backend slots** (Eulerian view) to keep traffic moving, predict loss of control early, and avoid oscillations.

Nity is built as a **kernel dataplane (eBPF)** plus a **deterministic control-plane**. The dataplane stays **O(1)** per connection event; the control-plane updates the “field” in discrete ticks and can fail-safe without blackholing.

---

## What Nity is (and is not)

**Nity is:**
- A **continuity controller** for Kubernetes traffic (prioritizes flow stability over “perfect routing”).
- **Deterministic**: every action is explainable by a finite set of physical rules.
- **Fast-sensing**: uses ms–s signals (e.g., PSI stall, kernel counters) to act before p95/p99 graphs tell the full story.
- **Fail-safe by design**: if the agent is stale or down, dataplane behavior remains coherent.

**Nity is not:**
- A generic “send traffic anywhere” router. Traffic only moves **within the equivalent backend set** for a given Service/route.
- A promise of infinite throughput. If there’s no spare capacity, the correct physics is **admission control** (backpressure) and/or **scaling**.

---

## Core principles (physics → behavior)

### Eulerian indifference (Axiom Zero)
The decision layer does not track “the user” or “the request.” It observes fields:
- **Pressure** (resistance to flow) at each backend
- **Conductance** (how much flow can pass per unit pressure)

### Pressure is more than latency
Pressure is a composite of forces that resist flow (queue/latency/errors, and optionally path/node signals). Errors act like leaks: they carry a disproportionate penalty.

### Viscosity prevents flapping
Nity never changes flow allocation abruptly. Slot updates are bounded by a **slew-rate** to damp oscillations.

### Continuity over perfection
When the system approaches loss of control, Nity protects continuity with:
- progressive isolation of sick backends
- controlled diagnostic drip
- **backpressure** at the boundary (soft → hard)
- slow recovery (hysteresis + “queue debt” paydown)

---

## Architecture (high level)

- **Dataplane (eBPF, per node):**
  - deterministic selection (laminar “gear”)
  - stickiness via conntrack LRU
  - atomic epoch flip (A/B tables)
  - admission enforcement (normal/soft/hard)
  - fail-safe modes (HOLD → FALLBACK)

- **Control-plane (agent, per node):**
  - computes pressure/TTF/PSI-derived pathology
  - updates the inactive slot table, then flips epoch atomically
  - publishes low-cardinality audit metrics (Prometheus-friendly)

Why per-node agent (DaemonSet)? Because sensors and actuators are node-local: it reduces control latency, avoids network blind spots, and improves failure containment.

---

## Regimes vs states (don’t mix them)

**Regimes** are high-level postures:
- **Laminar (normal):** stable flow, small corrections, high predictability
- **Transition (degradation):** signals worsen, firmer corrections, selective isolation
- **Crisis (survival):** protect the system; hard deny; strict safety
- **Recovery:** slow reopening; hysteresis; queue-debt paydown

**States** are the concrete machine modes enforced by dataplane:
- `admission_mode ∈ { normal, soft, hard }`
- `failsafe_mode  ∈ { normal, hold, fallback }`

Regimes *choose* states. Failsafe states can override regimes when the agent is stale.

---

## The critical real-world constraint: equivalence sets
Nity can only redistribute traffic **within the equivalent backend set** for a Service/route (same semantics, same selector).  
If a Service has **only one replica**, there is no alternative backend to offload to—Nity switches from redistribution to **admission control** and can emit a **scale hint** (optional) rather than pretending there is a “second route.”

---

## Telemetry policy
Nity’s control loop uses **ms–s** signals for real-time decisions.  
**Prometheus/Grafana** are used for **auditability** and low-cardinality summaries, not as primary sensors.

---

## Project status
This repository is under active development. 
- ✅ Core model: pressure/viscosity/admission/failsafe
- ✅ Documentation: constitution + regimes/states
- 🚧 Implementation:  dataplane + agent wiring (in progress)
- 🔜 Harness: invariant tests (sim/replay) for physics CI

---

## Contributing
Contributions are welcome, especially around:
- eBPF correctness + verifier-friendly patterns
- invariant testing (bounded dp cost, bounded remap, monotonic skew under stable topology)
- documentation clarity (regimes/states, operational rules)

If you submit a change that affects behavior, please describe which invariant(s) it preserves or strengthens.

---

## License
Planned: Apache-2.0 (or similar permissive license).  
(If `LICENSE` isn’t present yet, it will be added early to avoid ambiguity.)

---

## Name
**Nity** is the project nickname. **Continuity** describes its purpose: preserve the continuity of flow under stress.
