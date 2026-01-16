# Nity Configuration Knobs (MVP)

This file lists the knobs that must be explicit in the MVP so behavior is not implicit.

All values below are **defaults**. They exist so the system is operable on day 1.

---

## 1. Timing

- Dt: control tick period (e.g., 200ms)
- D:  robust derivative window (e.g., 5s)

- T_dwell: minimum time between state transitions (e.g., 2s)
- T_recover_min: minimum stability time before relaxing a strict state (e.g., 20s)

---

## 2. Failsafe thresholds

- tau1: HOLD threshold (e.g., 3s)
- tau2: FALLBACK threshold (e.g., 15s)

---

## 3. Admission thresholds

- T_safe: TTF safety threshold (e.g., 120s)
- T_hard: TTF hard threshold (e.g., 20s)
- G_min: conductance minimum for normal (calibrate per service)

---

## 4. Pressure weights

- W_Q: queue weight
- W_L: latency weight
- W_E: error weight
- E_ref: reference error rate
- eta: absolute error gate threshold
- K_E: absolute error penalty

---

## 5. Slot dynamics

- S_total: total slots per route group
- Delta_max: max slot delta per backend per tick (viscosity)

---

## 6. Soft admission budget

Token bucket per route group:
- B: burst capacity
- r: refill rate (tokens/sec)

---

## 7. PSI (stall) thresholds

- psi_cpu_some_threshold
- psi_cpu_full_threshold
- psi_mem_some_threshold
- psi_mem_full_threshold

And the sustained stall configuration:
- m: number of samples
- p: required fraction of "stall" samples

