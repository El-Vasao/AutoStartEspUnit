# MQTT audit: host vs hardware

Ось стендового аудита car-server. Host (`pio test -e native` в AutoStartEspUnit) закрывает **форму** контракта; стенд закрывает **провод** (SIM800 → брокер → subscriber).

## Уже на хосте

| Что | Набор |
|-----|-------|
| Парсинг `/cmd`, коды ошибок | `test_mqtt_command_parser`, `test_mqtt_commands` |
| JSON full/delta `state`, лимиты каталогов vs `CIPSEND_MAX` | `test_mqtt_status_builder`, `test_mqtt_hwmap` |
| Кадры / FSM без SIM800 | `test_mqtt_wire`, `test_mqtt_fsm` |

## Только железо / интеграция

| Что | Почему host не хватает | Сессия |
|-----|------------------------|--------|
| Birth: hashes → full `state` → `online=true`, retain | Нет брокера / CIP TX order | S1 |
| LWT / `online=false` | Нужен обрыв TCP | S1 (опц.) |
| Байт/мин idle (period + keepalive глазами sub) | Нет живого трафика | S2 |
| edge → delta `full:false` (реле/IN/VBAT/T, gap ≥250 ms) | CarEmu + ADC + CIP tele | S3 |
| Latency стимул EMU → `/state` | Сквозной путь | S3 |
| Cmd → 202/200 RTT | CIP serialize / QoS1 | S4 |
| Max live `state` ≤900 B | Реальные temps/имена | S1–S4 |
| Шум VBAT PWM vs ложные edge | Реальный ADC | S3 |

Правило: не дублировать parser/builder unit-тесты; каждый S* отвечает на вопрос, который зелёный native не закрывает.
