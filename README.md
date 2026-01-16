# Nity (Continuity)
**Deterministic traffic continuity for Kubernetes, inspired by Eulerian fluid mechanics.**  
Nity treats requests as **fluid** and backends as fixed points on a mesh. Instead of tracking “individual particles” (requests/users), it controls **pressure fields** and **flow capacity** to keep traffic moving smoothly under stress.

## What problem it solves
Most traffic failures aren’t “one bad request” — they’re **systemic loss of control**: queues grow, latency spikes, retries explode, nodes stall, and the cluster starts oscillating (flapping).  
Nity focuses on one thing: **continuity** — maintaining stable flow within safe limits, and degrading gracefully when physics says you’re out of capacity.

## Why “Eulerian fluids”
There are two ways to reason about flow:
- **Lagrangian:** track each particle (each request) → expensive, noisy, and often unstable at scale.
- **Eulerian:** observe the field at fixed points → stable, bounded cost, and closer to how control systems work.

Nity is Eulerian by design: it doesn’t “decide per request” in the control plane; it updates a **field** (routing capacity) based on measured pressure.

## How it works (high level)
Nity is split like real control systems:

- **Dataplane (eBPF):** fast, O(1), deterministic decisions on the hot path.  
  Think: *valves + pipes*.

- **Control-plane (agent):** periodic control loop that reads pressure signals and updates the field.  
  Think: *regulator adjusting valves with viscosity and hysteresis to avoid oscillation*.

### Core physical ideas (translated to systems)
- **Pressure:** not just latency — a weighted resistance signal (queue/latency/errors/stalls).
- **Conductance:** “how much flow the system can still pass” (global health proxy).
- **Viscosity:** bounded change per cycle (slew-rate) to prevent control flapping.
- **Backpressure:** protect the system by slowing/denying new admissions when needed.
- **TTF (Time-To-Failure):** “how long until loss of control?” beats “latency up 20%”.
- **PSI stall gate:** resource usage matters only when it causes **stall / lack of progress**.

## Features (current direction)
Nity aims to provide:
- **Pressure-driven traffic distribution** (capacity moves away from sick backends)
- **Stable control** (slew-rate + hysteresis; no random load “dice”)
- **Global backpressure modes** (soft/hard) to prevent retry storms
- **Fail-safe behavior** (field stays coherent if the agent dies)
- **Deterministic, explainable decisions** (every action maps to a physical cause)
- **Auditability** (Prometheus/Grafana can be used for summaries; not the primary sensor)

## What it is not
- Not “AI routing”
- Not a service mesh replacement
- Not a per-request policy engine
- Not a magic throughput generator (physics still wins)

## Status
This project is evolving around a “physical MVP”: a small set of laws that already guarantees continuity (bounded cost, stable allocation, backpressure, fail-safe).  
The full design is written as a “constitution” of physical invariants and measurable metrics.

## Philosophy
Most “AI for ops” tries to learn behavior from data. Nity tries to **encode constraints** so the system behaves like a stable physical process: predictable, bounded, and explainable — closer to *control theory* than *black-box optimization*.

## Contributing
If you like eBPF, control loops, SRE failure modes, or you want to stress-test the invariants with real scenarios (retry storms, churn, stalls), contributions are welcome. Start by opening an issue describing:
- the failure mode,
- the observable signals,
- and what invariant should hold.

## License
TBD (project intends to be open source).
