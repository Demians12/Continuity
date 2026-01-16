# RFC 0006 — Invariants Harness (Physics CI): Simulation + Replay

- **Status:** Draft (MVP)
- **Owner:** Nity / Continuity
- **Scope:** Define the test harness that validates Nity’s invariants under stress

---

## 1. Context

Nity is not “just code”; it is a set of **physical invariants** that must remain true under stress:
- bounded dataplane cost,
- bounded remaps under churn,
- monotonic skew improvement (under stable topology),
- no silent blackholes post-admission,
- stable transitions (no flapping).

A harness is required so regressions are detected early, not in production.

---

## 2. Goals (MVP)

1) Provide repeatable scenarios that generate synthetic signals and traffic.
2) Measure invariants and fail fast on violations.
3) Allow replay of captured “flight recorder” traces.
4) Produce a simple report that CI can parse.

---

## 3. Non-goals (MVP)

- Perfect emulation of all Kubernetes network stacks.
- Full-blown formal verification.
- Huge benchmark suites (start small and sharp).

---

## 4. Scenario model

A scenario defines:
- topology (route groups, backend sets, nodes)
- time series of signals (latency, error, PSI stall, churn events)
- expected invariants (thresholds)

Example scenario list (MVP):
- `overload.yaml`
- `churn.yaml`
- `noisy-latency.yaml`
- `agent-death.yaml`
- `retry-storm.yaml` (optional)

---

## 5. Invariants (minimum set)

### 5.1 Bounded dataplane cost
```text
p99(dp_event_cost_ns) <= B_ns
```

### 5.2 No-blackhole post-admission
```text
admitted = delivered + explicitly_rejected
```

### 5.3 Monotonic skew (stable topology, non-overload)
```text
skew(t+1) <= skew(t) + ε
```

### 5.4 Bounded remap under small churn (when consistent mapping exists)
```text
remap_percent <= ρ_max
```

### 5.5 Bounded flapping (state transitions)
```text
flap_rate_per_min <= F_max
```

---

## 6. Outputs

Harness outputs:
- a JSON summary with pass/fail per invariant
- optional time-series CSV for plotting
- a short Markdown report for humans

---

## 7. Acceptance criteria (MVP)

- CI can run the harness in a deterministic way (seeded randomness).
- A single regression fails CI with a clear invariant name.
- Reports include:
  - worst-case values,
  - the time window where the violation happened,
  - a pointer to relevant trace events.

---

## 8. References

- Constitution: Article 24 (Invariant Harness)
- Control loop: RFC 0001
- Backpressure: RFC 0002
- Epoch flip/failsafe: RFC 0003
