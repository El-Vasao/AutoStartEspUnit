# Стенд / MQTT-аудит

Снимок результатов аудита MQTT (car-server) на железе, 2026-11-24.

| Файл | Содержание |
|------|------------|
| [mqtt-audit-report.md](mqtt-audit-report.md) | Compliance, трафик, latency, решения |
| [mqtt-audit-hw-gap.md](mqtt-audit-hw-gap.md) | Host-тесты vs только железо |
| [mqtt-audit-analysis.json](mqtt-audit-analysis.json) | Сырые метрики analyze_jsonl по S1–S4 |

Скрипты и jsonl-захваты живут в соседнем проекте **CarEmu**:

- `../CarEmu/tools/bench/bench.py`
- `../CarEmu/tools/bench/run_audit_sessions.py`
- `../CarEmu/tools/bench/analyze_jsonl.py`
- `../CarEmu/tools/bench/audit-captures/session-S*.jsonl`

Контракт: [modules/mqtt.md](../modules/mqtt.md). Чеклист функций на стенде: [TESTING.md](../TESTING.md).
