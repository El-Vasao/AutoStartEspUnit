# `mqtt`: MqttFsmClient, команды и статус

## Роль
MQTT-публикация статуса и подписка на команды выполняются **встроенным неблокирующим клиентом** (`MqttFsmClient`) через обёртку `MQTTClient`. Транспорт — `Client&` из GSM (SIM800 TCP).

Основные файлы:
- `include/mqtt/MQTTClient.h`, `src/mqtt/MQTTClient.Core.cpp`
- `include/mqtt/MqttFsmClient.h`, `src/mqtt/MqttFsmClient.cpp`
- парсинг команд: `include/mqtt/MqttCommandParser.h`, `src/mqtt/MqttCommandParser.cpp`
- JSON статуса: `src/mqtt/MqttStatusBuilder.cpp`

## Контракт
### Идентификация устройства
- Устройство **идентифицируется только по топикам**.
- В payload **нет** `deviceId`/`unitId` и т.п.

### Топики (из `mqtt.topic_prefix`)
Пользователь настраивает только **`mqtt.topic_prefix`** (без trailing `/`), например `car/Subaru`.
Суффиксы фиксированы в прошивке (`MqttTopics` в `Constants.h`):

| Суффикс | Топик | Назначение |
|---------|-------|------------|
| `/avail` | `{prefix}/avail` | Presence: retained `online` / `offline` (LWT) |
| `/status` | `{prefix}/status` | JSON телеметрия |
| `/cmd` | `{prefix}/cmd` | Входящие команды |
| `/reply` | `{prefix}/reply` | Ответы (`list_programs`) |

Пустой `topic_prefix` → без LWT/online/subscribe/publish.

### Доставка (политика по умолчанию)
Реализация: `src/mqtt/MQTTClient.Core.cpp` + CONNECT в `src/mqtt/MqttFsmClient.cpp`.
- **LWT** на `{prefix}/avail`: текст `"offline"`, **retained**, **Will QoS = 1**.
- После **MQTT Connected**: retained `"online"` на `/avail` (публикуется **независимо** от JSON-статуса).
- Периодический JSON на `/status`: **QoS 0**, **без retained**, раз в `mqtt.publish_interval_sec`.
- Подписка на `/cmd`: **QoS 0**.
- Ответ `list_programs` на `/reply`: QoS 0, not retained.
- Clean disconnect: один attempt retained `"offline"` на `/avail` перед MQTT DISCONNECT.

### Anti-hang
- Вызывающий код гоняет `MQTTClient::loop()` кооперативно вместе с GSM; каждый `tick()` имеет байтовые лимиты RX/TX.
- `CellularCore` не вызывает `mqtt.loop()`, пока `gsm.tcpBusBusy()` (send-epoch / CIPSTART / CIPSHUT).
- После успешного status publish — `Core::cooperate()` для SoftAP fairness.
- `Client::connect` неблокирующий; reconnect kick только из Idle/Error.
- Публикация статуса не блокируется бесконечно; если локальное измерение+стейджинг JSON заняло **> ~10 ms**, это **логируется**, соединение **не разрывается** автоматически из-за этого порога.

### Reconnect и 2-phase connect (SIM800)
- Сначала поднимается TCP (`Client.connected()` после `CONNECT OK` модема); `CIPSTART` с cooldown и без mid-send.
- Затем MQTT CONNECT только поверх живого TCP.
- После `"online"` на `/avail` — settle ~1.5 s до первого JSON на `/status`.

Протокол MQTT в CONNECT: при необходимости совместимости с брокером можно включить режим имени **`MQIsdp` / MQTT 3.1** через compile-time (см. `MQTTClient.Core.cpp`): `-DMQTT_VERSION=MQTT_VERSION_3_1`.

### Диагностика RX (SIM800)
После ошибок установления связи возможен диагностический HTTP GET на `1.1.1.1:80` (см. путь восстановления в GSM/MQTT-коде) для проверки входящего трафика.

## Память
- В callbacks нет динамических аллокаций под команды; размер входного JSON ограничен `JsonBytes::Mqtt::CMD_JSON_MAX`.
- JSON статуса собирается потоково в TX-буфер FSM (`publishPrintedMeasured`), без большого постоянного payload в `.bss`.
- Wire caps: `MqttFsmClient::TX_MAX = 1024`, `JsonBytes::Mqtt::STATUS_PAYLOAD_MAX_BYTES = 900` (и `LIST_PROGRAMS_JSON_MAX`). Поля статуса **не усекаются**; при `measured > max` publish fail + log.

## Сообщения

### 1) Command (вход, `{prefix}/cmd`)
Команды — **JSON-объект** (строка UTF-8). Поддерживаются:
- `{"action":"run","program":<1..255>}` — запуск программы по id.
- `{"action":"run_program","program":<1..255>}` — то же (синоним для совместимости с документацией/интеграциями).
- `{"action":"list_programs"}` — запросить список программ.

Семантика:
- `run` / `run_program` → `AppPorts.control.startProgram(ctx, programId)` (см. `MQTTClient.Core.cpp`).
- `list_programs` → публикация в `{prefix}/reply` (см. ниже).

### 2) Connection markers (выход, `{prefix}/avail`)
Краткие не-JSON сообщения presence:
- retained `"offline"` — LWT при unclean disconnect; также один attempt при clean disconnect;
- retained `"online"` — после успешного CONNECT.

### 3) StatusSnapshot (периодический JSON, выход, `{prefix}/status`)
Формируется `emitMqttStatusJson()` в `src/mqtt/MqttStatusBuilder.cpp`. Поле **`schema` не используется**. QoS 0, **не** retained.

Порядок полей = порядок emit (нормативный контракт):

| # | Поле | Тип | Смысл |
|---|------|-----|--------|
| 1 | `uptime` | number | секунды аптайма |
| 2 | `mode` | string | имя режима (до 15 символов) |
| 3 | `voltage` | number или `null` | напряжение |
| 4 | `engineRunning` | bool | `Core::isEngineRunning()` (voltage hysteresis или digital input) |
| 5 | `inputsById` | object | ключи `Pin::INPUT_IDS` (1001–1003), значение bool |
| 6 | `relaysById` | object | ключи `Pin::RELAY_IDS` (2001–2005), значение bool |
| 7 | `tempSensorsById` | object | ключ = id сенсора (`BaseConfig.sensors[].id`, дефолт 3001–3003); значение `{ valid, lastMs, t }`; несмапленные слоты не эмитятся |
| 8 | `current_program` | number | только если программа выполняется |
| 9 | `last_program` | number | всегда |
| 10 | `runtime` | object | `inputTriggersById`, `tempTriggersById` — ключи id триггеров, значение bool |
| 11 | `last_error` | string | экранированная строка; пустая если ошибок нет (`ErrorCode::NONE` не публикуется как `"OK"`) |

Пример:

```json
{
  "uptime": 12345,
  "mode": "NORMAL",
  "voltage": 12.4,
  "engineRunning": true,
  "inputsById": {"1001": false, "1002": true, "1003": false},
  "relaysById": {"2001": false, "2002": true, "2003": false, "2004": false, "2005": false},
  "tempSensorsById": {
    "3001": {"valid": true, "lastMs": 1000, "t": 21.5},
    "3003": {"valid": true, "lastMs": 2000, "t": 18.0}
  },
  "current_program": 2,
  "last_program": 2,
  "runtime": {
    "inputTriggersById": {"10": true},
    "tempTriggersById": {"20": false}
  },
  "last_error": ""
}
```

Неймспейс id по умолчанию: входы `1xxx`, реле `2xxx`, сенсоры `3xxx`.

### 4) ProgramsList (ответ на `list_programs`, выход, `{prefix}/reply`)
Объект вида:

```json
{"programs":[ … ]}
```

(сериализация из `Config::emitProgramListWrapped()` / `program_json::emitProgramListWrappedPrint`).
