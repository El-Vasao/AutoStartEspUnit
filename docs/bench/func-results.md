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
6. **Full `state` oversized after long bench (2026-11-25, domain 5 pytest):** base full ~642 B; `err` ring (capacity 5) filled with repeated `MQTT connection failed` (48) + leftover `Engine did not start` (83) → retained ~882 B; next full publish measured **943/900** → birth stuck (no `online=true`). **FW fix (2026-11-26):** `ErrorManager::set` reactivates inactive same-code (no duplicate slots); `STATUS_PAYLOAD_MAX_BYTES` **900→1024**; `publishStatus` failsafe trims `err` to active then drops `err` so birth can complete. Capture: `CarEmu/tools/bench/audit-captures/domain5-triggers.txt`.
7. **Domain 5 harness (2026-11-25):** MQTT `set` trigger/temp_trigger uses numeric **`ref`** (config id), not `id` (correlation). CarEmu serial has **`in1`/`in2` only** — `in3 l|h` → `err unknown`; wire input triggers to IN1 for bench.
8. **prg flash → MQTT LWT (2026-11-25, 6.14):** sync LittleFS inside MQTT `commit_` starved modem RX → `online=false`. **FW fix (2026-11-26):** debug `cfg`/`prg` commit arms `FlashCommitCoordinator` (SoftAP parity, includes `rebuildProgramIndex`); final `/reply` after `flashCommit.tick`. Harness settle still useful under load.
9. **Cfg profile reuse (2026-11-26):** harness `bench_profiles.ensure_profile` / `apply_cfg` + session-once `require_debug_cfg` + idle-skip `settle_idle`. Dead `if "N.M"==…` monoliths slimmed (`03b` 2818→~330, `04b` 2853→~280, `08` 357→~110, `09ext` 2627→~250, `06/07ext` rewritten). Tiered apply: MQTT `set` for runtime; put without reboot for Allowed; put+reboot for wiring/`start_method`/boot enabled / AP timeout / NTP. Domain 4 harness fixes: `run program=0` → **400** (not 422); program JSON needs `step` + `relay_id`/`input_id` (not `relay`/`TOGGLE`). SoftAP domains 13–14: no auto cfg. **Before (prior domain audits):** domain 5 put+reboot per case ~1–3 min each; domains 3/5/6/7 dominated by cfg cycles. **After (expected):** fingerprint skip → ×2–3 wall-clock on cfg-heavy domains; prep/io/delta modest settle gain. **Stand re-verify (this session):** AS UART (COM5) absent — only COM3/COM1 listed; `pytest --collect-only` → **231** items green. Re-run `pytest -m "<domain>"` when AS+MQTT stand is up; capture before/after wall times next live pass.
