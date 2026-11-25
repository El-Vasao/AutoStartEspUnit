# Функциональный стенд — результаты

Firmware tip: AutoStart **1.0.32+** (`debug` build for D/E). Prefix `car/emulator`. Date: 2026-11-24.

Captures: `CarEmu/tools/bench/audit-captures/func-*.jsonl` / `func-DE-live4.txt`  
Harness note: `bench.py` cmd `id` must be unique across sessions (fixed: time-prefixed); otherwise device id-ring returns cached `200` and **does not run**.

Verdicts: thin `/reply` = HTTP-like `code` only; **reason** is in `/state` → `err` (`[{code,msg,active},…]`). Full `status` keeps history after `markDelivered`.

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

**Путь:** debug FW + `cfg get` → patch (IN1 type/`threshold_rpm`) → `cfg_put_bytes` → MQTT `reboot` (не один `cfg set`). Скрипт: `CarEmu/tools/bench/run_func_de.py`. Capture: `audit-captures/func-DE-live4.txt` (**0 failed / 22**).

`reboot_required` **не** гасит MQTT/GSM — только отображаемое имя режима ([BEHAVIOR.md](../BEHAVIOR.md)); после `cfg put` нужен реальный `reboot`, иначе менеджеры/`enabled` не перечитываются с boot.

| # | Case | Result | Notes |
|---|------|--------|-------|
| D0 | flash debug FW | **pass** | manual |
| D0b | MQTT debug smoke | **pass** | set/get/put/prg get/404 |
| D1 | rpm success | **pass** | put+reboot; `run 1` → 200, last=1, K1+K2 |
| D2 | rpm fail nofire | **pass** | reply **500**; full status `err` code 83 active |
| E1 | input success | **pass** | mid-crank `in1 h` → 200 |
| E2 | input fail | **pass** | `nofire`+`in1 l` → 500, err 83 |
| C1-reg | timed after restore | **pass** | V≈14.01, K1+K2 |
| prg get/put 200 | | **pass** | scratch id 200 |
| prg delete 200 | | **pass** | |
| cfg set 404 | | **pass** | |
| modem_reboot | | **pass** | ack 202; MQTT recover after hard-drop+CFUN |

Helpers: `cfg_put_bytes` raw≈120 B; `wait_reply` skips 202; settle after commit before `reboot` (иначе SIM800 `rx_incomplete`).

## F. MQTT contract (live)

| Case | Result | Notes |
|------|--------|-------|
| seq / list / hwmap / trigmap | pass | 200 |
| preempt `run 1` → `run 2` | pass | handoff; old id **500**, new **200** |
| stop mid-run | pass | stop at step 3/8 → `allOff`, relays false; run id **500** |

## G. Gap (Stop / debug tail / LWT)

Скрипт: `CarEmu/tools/bench/run_func_gap.py`. SoftAP **не** используется (см. [README.md](README.md) § SoftAP).

| # | Case | Result | Notes |
|---|------|--------|-------|
| S1 | «Стоп» после успеха | pending | `run 1` → idle `stop` keeps relays → `run 2` clears K1/K2/K5 |
| D1 | `cfg set` valid | pending | rpm → get → timed |
| D3 | id-ring replay | pending | same id → cached code + AS `id-ring replay` |
| D2 | `prg reset` | pending | list has 1 and 2 |
| D4 | `cfg reset` + restore | pending | put restore **before** reboot (`setup_required=false`); never reboot on factory flash |
| L1 | LWT / hard-drop | pending | `modem_reboot`: no graceful `pub online false`; online false→true |

Voice/SMS/DTMF и broker TCP drop mid-SEND — вне tooling (field log).

## Findings to track

1. Idle `stop` after success — documented in BEHAVIOR / TESTING / USAGE (not a harness bug).
2. Bench cmd ids must not reuse `b0001…` across processes (fixed: time-prefixed).
3. Between cases reset with `run 231` (Все выкл) + `e vbat auto` to avoid `already_running` contamination.
4. FW (2026-11-24 scope): `modem_reboot` hard-drop MQTT before CFUN; full `state.err` keeps history; debug commit/delete settle RX; id-ring replay log.
5. SoftAP is human-only for bench automation; `cfg reset` + reboot with factory `setup_required` drops MQTT until operator restores.
