# Функциональный стенд — результаты

Firmware tip: AutoStart **1.0.30**. Prefix `car/emulator`. Date: 2026-11-24.

Captures: `CarEmu/tools/bench/audit-captures/func-*.jsonl`  
Harness note: `bench.py` cmd `id` must be unique across sessions (fixed: time-prefixed); otherwise device id-ring returns cached `200` and **does not run**.

## A. Prep

| Check | Result |
|-------|--------|
| AS COM5 / EMU COM3 / MQTT | pass |
| `online=true` | pass |
| Program id 1 «Запуск» | pass |
| Relays off, IN2 inactive | pass |
| T1–T3 `ok` | pass |

## B. Smoke / IO

| # | Case | Result | Notes |
|---|------|--------|-------|
| B1 | CarEmu status / follow | pass | |
| B2 | VBAT steps | pass | |
| B3 | K1/K2/K4/K5 helpers | pass | EMU edges |
| B3b | K3 | covered in C | |
| B4 | temps t1/t2/t3 | pass | 5 / 21.5 / 40 |

## C. Timed start

| # | Case | Result | Notes |
|---|------|--------|-------|
| C1 | success `follow` | **pass** | `last=1`, starter edged, V~14, K1+K2 left on |
| C2 | stop after success (idle) | **fail** | `ProgramExecutor::stop()` calls `allOff` **only if `_running`**; after successful «Запуск» MQTT `stop` leaves ACC/IGN on. Mid-run stop is OK (see F). |
| C3 | `nofire` | **pass** | run → **500**, relays off |
| C4 | block IN2 | **pass** | `Engine start blocked by interlock`, 500, no starter |
| C5 | already running (V≥13.2) | **pass** | `stop=already_running`, no crank |
| C6 | freeze `vbat 12.0` | **pass** | crank tried, run → 500 |
| C7 | `stall` / 3s | **inconclusive** | stall during `wait_after_start` looked like `voltage_low` retries → 500; need longer `stall_ms` after confirmed success |

## D–E. RPM / input start

**Deferred** — `start_method` only via SoftAP/`config`, not MQTT `set`.

## F. MQTT contract (live)

| Case | Result | Notes |
|------|--------|-------|
| seq / list / hwmap / trigmap | pass | 200 |
| preempt `run 1` → `run 2` | pass | handoff; old id **500**, new **200** |
| stop mid-run | pass | stop at step 3/8 → `allOff`, relays false; run id **500** |

## Findings to track

1. **Idle `stop` does not clear leftover relays** after successful start (code vs TESTING.md expectation).
2. Bench cmd ids must not reuse `b0001…` across processes.
3. Between cases reset with `run 231` (Все выкл) + `e vbat auto` to avoid `already_running` contamination.
