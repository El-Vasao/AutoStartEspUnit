# Функциональный стенд — результаты

Firmware tip: AutoStart **1.0.31** (`debug` build for D/E). Prefix `car/emulator`. Date: 2026-11-24.

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
| C2 | stop after success (idle) | **expected** | Documented: `stop()` allOff only while running; after success use program «Стоп». Mid-run stop OK (F). |
| C3 | `nofire` | **pass** | run → **500**, relays off |
| C4 | block IN2 | **pass** | `Engine start blocked by interlock`, 500, no starter |
| C5 | already running (V≥13.2) | **pass** | `stop=already_running`, no crank |
| C6 | freeze `vbat 12.0` | **pass** | crank tried, run → 500 |
| C7 | engine stop after success | **pass** | maxV~14.1 engine=true → `e engine stop` → V~12.4 engine=false; ACC/IGN remain |

## D–E. RPM / input start

**Путь:** `python build.py debug -u` + MQTT `cfg set` / `cfg put` (без SoftAP). После set/put — `rebootRequired` как SoftAP; для чистого старта менеджеров — `reboot`.

| # | Case | Result | Notes |
|---|------|--------|-------|
| D0 | flash debug FW | **blocked** | `pio upload` COM5/COM3: no serial data (нужен boot/USB flash вручную) |
| D1 | `cfg set vehicle.start_method rpm` + reboot | pending | bench: `cfg set vehicle.start_method rpm` |
| D2 | RPM start success | pending | after D1 |
| E1 | `cfg set vehicle.start_method input` + reboot | pending | |
| E2 | input start | pending | |

Helpers: `Bench.cfg_put_bytes` / `cfg set` / `cfg get` + `collect_chunks("cfg")` in CarEmu `tools/bench/bench.py`. Release FW → `400` + hint.

## F. MQTT contract (live)

| Case | Result | Notes |
|------|--------|-------|
| seq / list / hwmap / trigmap | pass | 200 |
| preempt `run 1` → `run 2` | pass | handoff; old id **500**, new **200** |
| stop mid-run | pass | stop at step 3/8 → `allOff`, relays false; run id **500** |

## Findings to track

1. Idle `stop` after success — documented in BEHAVIOR / TESTING / USAGE (not a harness bug).
2. Bench cmd ids must not reuse `b0001…` across processes (fixed: time-prefixed).
3. Between cases reset with `run 231` (Все выкл) + `e vbat auto` to avoid `already_running` contamination.
