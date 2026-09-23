# `mqtt`: MqttFsmClient, команды и статус

## Роль
MQTT-публикация статуса и подписка на команды выполняются **встроенным неблокирующим клиентом** (`MqttFsmClient`) через обёртку `MQTTClient`. Транспорт — `Client&` из GSM (SIM800 TCP).

Основные файлы:
- `include/mqtt/MQTTClient.h`, `src/mqtt/MQTTClient.Core.cpp`, `src/mqtt/MQTTClient.Commands.cpp`
- `include/mqtt/MqttFsmClient.h`, `src/mqtt/MqttFsmClient.cpp`
- парсинг команд: `include/mqtt/MqttCommandParser.h`, `src/mqtt/MqttCommandParser.cpp`
- JSON статуса: `src/mqtt/MqttStatusBuilder.cpp`
- лимиты cmd/reply: `MqttCmd::*` в `Constants.h`

## Контракт
### Идентификация устройства
- Устройство **идентифицируется только по топикам**.
- В payload **нет** `deviceId`/`unitId`.
- Один device = один `topic_prefix`.

### Топики (из `mqtt.topic_prefix`)

| Суффикс | Топик | Назначение |
|---------|-------|------------|
| `/avail` | `{prefix}/avail` | Presence: retained `online` / `offline` (LWT) |
| `/status` | `{prefix}/status` | JSON телеметрия (периодика) |
| `/cmd` | `{prefix}/cmd` | Команды → device (QoS 0, не retained) |
| `/reply` | `{prefix}/reply` | Ответы ← device (QoS 0, не retained) |

**Offline = нет команд:** QoS 0, `cleanSession=true`, `/cmd` без retain. Клиент шлёт `/cmd` только при `avail=online`.

### Единый `/reply` (все `cmd`)

Слои:
- **Ingress** (`tryAckIngress_`): только parse + место в FIFO → **ack** с `code` (`202` или reject). Handlers не вызываются.
- **Dispatch**: только **финал** с итоговым `code` (+ fat body у `list`/`status`).

1. **Ack** — по факту приёма кадра с валидным `id`:
   - принято → `{id, code:202}`, команда в FIFO;
   - не принято → один `/reply` (`400` / `503` / …) и **без** финала.
2. **Финал** — после исполнения, **ровно один** `/reply` с итоговым `code`:
   - thin `{id, code}` — `set` / `stop` / ошибки / финал `run`;
   - fat — `list` (`programs`), `status` (объект `status` = full snapshot). Команда `status` **не** публикует Tele `/status`.

Нет третьих сообщений (`run` без промежуточного `201`).

**Ctrl serialize:** следующий Ctrl publish (ack или final) стейджится только когда в TX нет другого Ctrl — один CIPSEND-epoch на control reply за раз.

### Очереди cmd (`MqttCmd::*`)
- Inbound FIFO **`INBOUND_DEPTH=8`**: по порядку, по одной; dispatch не стартует, пока Ctrl ещё outbound.
- Pending reply **`PENDING_REPLY_DEPTH=4`** (retry если Ctrl занят).
- Периодический `/status` (Tele) — низший приоритет: только когда inbound/pending пусты, нет активного cmd и нет Ctrl в TX; Tele не занимает последний слот очереди FSM (FIFO send).

**Нет silent drop:** на кадр с валидным `id` на `{prefix}/cmd` устройство всегда отдаёт ≥1 `/reply` с тем же `id`. Исключение — oversized payload без извлекаемого `id`.

### Транспорт (SIM800)
- Один MQTT-кадр = один атомарный `AT+CIPSEND` (`write` только буферизует, `flush`/`flushSend` стартует send).
- RX ring overflow → reconnect (не drop-oldest — иначе desync framing).
- `+IPD` framing exclusive while matching/reading; last payload byte never falls through to raw sniffer; `CIPHEAD=1` required.
- MQTT RX stall: валидный неполный кадр → fail-closed reconnect (`rx_incomplete`); junk head → byte-hunt.
- `mqtt.loop()` always when READY; mid-CIPSEND TX no-op, RX drained.
- Ctrl serialize: ack/final wait MQTT Ctrl queue and `gsm.tcpBusBusy()`.
- PUBLISH not on `/cmd` — ignore (no reconnect).

### Доставка
- LWT `/avail` `"offline"` retained Will QoS1; после connect — `"online"` (после SUBACK или SUBACK-timeout с retry SUBSCRIBE).
- `/status` QoS0, период `publish_interval_sec` (если Tele-idle).
- `/cmd` и `/reply` QoS0.

### 1) Command (`{prefix}/cmd`)

```json
{"id":"7f3a","cmd":"run","program":2}
{"id":"7f3a","cmd":"stop"}
{"id":"7f3a","cmd":"list"}
{"id":"7f3a","cmd":"status"}
{"id":"7f3a","cmd":"set","name":"thermostat","enabled":true}
{"id":"7f3a","cmd":"set","name":"input","ref":1001,"enabled":true}
{"id":"7f3a","cmd":"set","name":"trigger","ref":10,"enabled":false}
{"id":"7f3a","cmd":"set","name":"temp_trigger","ref":20,"enabled":true}
{"id":"7f3a","cmd":"set","name":"battery_saver","enabled":false}
{"id":"a1","cmd":"set","name":"wifi_ap","enabled":true}
```

| Поле | Правило |
|------|---------|
| `id` | 1..`MqttCmd::ID_MAX_LEN` (16) |
| `cmd` | `run` \| `stop` \| `list` \| `status` \| `set` |
| `program` / `name` / `ref` / `enabled` | по cmd |

### 2) Reply (`{prefix}/reply`)

```json
{"id":"b2","code":202}
{"id":"b2","code":200}
{"id":"b2","code":200,"programs":[…]}
{"id":"b2","code":200,"status":{"full":true,…}}
```

| code | Смысл |
|------|--------|
| **202** | Ack: принято в FIFO |
| **200** | Успешный финал |
| **400** | Не распознана / parse / args (только ack-фаза reject) |
| **404** | not found |
| **422** | rejected / start failed / reply too large |
| **500** | авария исполнения `run` |
| **503** | inbound полна — не принята |

`run`: `202` → финал `200`/`500` после lifecycle — в том числе при синхронном `finish()` внутри `start()` (одношаговые программы); `422` если старт не удался.  
Клиент: ждать `202` (~10 с) как факт приёма; финал — своим таймаутом. Повтор с тем же `id` обрабатывается заново (idempotency LRU нет).

### 3) Connection markers (`{prefix}/avail`)
retained `"offline"` / `"online"`.

### 4) StatusSnapshot (`{prefix}/status`)
`emitMqttStatusJson` / delta. QoS 0. Маркер `full` true/false. Периодика / first после connect. `cmd=status` отвечает fat `/reply`, Tele не форсирует.

Значимые изменения: `mode`, `engineRunning`, `last_error`, relays/inputs, triggers, program fields; `voltage` / temp — ε из `JsonBytes::Mqtt`.

### Вне scope
QoS1, текстовые `err` на wire, persist LRU, ACL, legacy API.
