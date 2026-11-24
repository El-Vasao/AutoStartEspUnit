# MQTT protocol audit report (car-server)

Date: 2026-11-24. Prefix `car/emulator`, broker `m5.wqtt.ru:10162`.  
Repo tip at audit: AutoStartEspUnit `FIRMWARE_VERSION_STR` **1.0.30** (`Version.h`; board already flashed, not re-flashed).  
Captures: `tools/bench/audit-captures/session-S{1..4}.jsonl`.  
Tools: `tools/bench/run_audit_sessions.py`, `tools/bench/analyze_jsonl.py`.  
Host vs iron map: [`mqtt-audit-hw-gap.md`](mqtt-audit-hw-gap.md).

## 1. Coverage: host vs hardware

| Area | Host | This stand |
|------|------|------------|
| cmd parse / reply codes | yes | S4 live RTT |
| full/delta JSON shape | yes | S3 live deltas |
| Birth / retain / online | no | S1 |
| Idle B/min + period | no | S2 |
| edge → delta + gap | no | S3 |
| cmd→202/200 via SIM800 | no | S4 |

## 2. Compliance vs `mqtt.md`

| Check | Result | Evidence |
|-------|--------|----------|
| Topic suffixes only; no `deviceId`/`unitId` in state | **pass** | All sessions |
| Birth order: hashes → full `state` → `online=true` | **pass** | S1 after reboot (see §3) |
| `online=true` only after full state left queue | **pass** | state→online **559 ms** |
| Retained catalog = 8 hex | **pass** | `99faf373` / `77157f10` / `a9c50032` |
| Catalog body via list/hwmap/trigmap, not retain hash | **pass** | S4 bodies 734 / 347 / 414 B |
| Delta `full:false`, omit unchanged keys | **pass** | S3 deltas mostly `io`+`uptime` |
| `seq` monotonic in session | **pass** | S3 seq 2…15 no gaps |
| Edge gap ≥ 250 ms | **pass** | min gap **262 ms** (seq 3→4) |
| Reply thin `{id,code[,seq]}` | **pass** | S4 |
| Codes 202 then 200 | **pass** | S4 all cmds |
| `status` reply includes `seq` | **pass** | `b0002` → seq 2 |
| `reboot` only 202 (no final 200) | **pass** | S1 |
| `state` body ≤ 900 B | **pass** | max **589** B |
| Frame ≤ 1600 CIPSEND | **pass** | largest app body 734 B programs |
| Voltage edge ≥ ~0.5 V | **pass** | S3 slew produced deltas |
| Small 0.1 V step alone | **pass** | `vbat 12.7` no dedicated delta |
| Temp edge ≥ 1 °C | **pass** | S3 wait was too short (3 s); live recheck + fixed script: deltas at 5 °C / 25 °C |
| LWT / broker TCP drop | **not run** | optional |

## 3. Birth (S1)

After `reboot` → 202:

| Interval | ms |
|----------|-----|
| reboot pub → `online=false` | ~34163 (reboot + modem bring-up) |
| `online=false` → programs hash | 3941 |
| programs → hwMap | 199 |
| hwMap → trigMap | 164 |
| trigMap → full state | 479 |
| full state → `online=true` | 559 |
| `online=false` → `online=true` | **5342** |

Live birth order matches the contract. Subscriber “retained dump” on connect collapses to one timestamp — measure birth from post-reboot stream, not from first subscribe batch.

## 4. Traffic

| Session | Duration | MQTT payload bytes* | Notes |
|---------|----------|---------------------|-------|
| S2 idle | 10.2 min | 1289 (incl. retain on connect) | One **period** full `state` ~600.2 s after previous birth |
| S3 edges | 0.74 min | 1430 | 1 full + **13** deltas |
| S4 cmds | 0.53 min | 3267 | cmds + replies + catalog bodies |

\*Subscriber view (`prefix/#`), UTF-8 payload sizes; MQTT/TCP headers and modem keepalive **not** included.

**Idle estimate (steady):** ~589 B full state / 600 s ≈ **59 B/min** application `/state`, ≈ **3.5 KB/h**. Keepalive (30 s) dominates modem airtime but is invisible on the MQTT app topics.

**Active edges:** delta avg **62.5** B (p95 82). During fast VBAT slew, ~13 deltas / ~40 s — expected while voltage is moving through the 0.5 V edge threshold.

## 5. Latency

| Path | Result |
|------|--------|
| Birth `false`→`true` | ~5.3 s |
| cmd → 202 (S4) | avg **1294** ms, p95 **2010**, max 2010 |
| cmd → 200 (S4) | avg **2037** ms, p95 **2590**, max 2590 |
| EMU stimulus → delta (S3) | avg **2386** ms, p95 **6767**, max 6767 (includes CarEmu/ADC slew) |

`ts` is PC receive time; absolute device→broker RTT is not measured. Order-of-magnitude and relative comparisons are valid.

## 6. Top-3 optimization / follow-ups

1. **Idle already lean** — `publish_interval_sec=600` + no spurious idle deltas. Further app-topic savings are small vs modem keepalive; do not optimize full JSON for idle KB/h first.
2. **Cmd RTT ~1–2 s** — dominated by SIM800 `CIPSEND` serialize / QoS1. Shrinking reply further will not help much; avoid stacking cmds; phone UX should expect ~2 s to final 200.
3. **S3 temp waits** — DS18B20 poll is 5 s active / 15 s idle; audit script must wait ≥16 s after `e t1` (fixed in `run_audit_sessions.py`). False fail in first S3 pass was timing, not a dead bus.

Honorable: rapid VBAT slew generates many short deltas — fine on bench; on a car, fewer 0.5 V edges. Delta payload is already small (~60 B).

## 7. Decision

| Action | When |
|--------|------|
| Keep period 600 s / edge 250 ms / 0.5 V / temp edge 1 °C | keep |
| Firmware/Android protocol changes for traffic | **defer** — no clear win from these numbers |
| Resume functional bench plan (timed/rpm/input start) | next |

Functional start/stop/GSM/OTA plan remains deferred (Cursor plan `bench_basic_test_plan`).
