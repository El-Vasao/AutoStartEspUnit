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

### Топики (берутся из конфигурации)
Источник — `BaseConfig.mqtt.*` (`include/config/ConfigTypes.h`):
- `mqtt.cmd_topic`: входящие команды (выделенный топик под устройство)
- `mqtt.status_topic`: статус и ответы (выделенный топик под устройство)

Если `status_topic` пуст, Last Will не задаётся и retained `"online"` не публикуется.

### Доставка (политика по умолчанию)
Реализация: `src/mqtt/MQTTClient.Core.cpp` + CONNECT в `src/mqtt/MqttFsmClient.cpp`.
- **LWT** на `mqtt.status_topic`: текст `"offline"`, **retained**, **Will QoS = 1** (задаётся в CONNECT-пакете).
- После перехода в **MQTT Connected** отправляется **один раз** retained `"online"` (QoS приложения через этот клиент всегда **0** для PUBLISH; см. ограничения FSM).
- Периодический JSON-статус: **QoS 0**, **без retained**, раз в `mqtt.publish_interval_sec` (первая попытка после connect не ждёт полного интервала).
- Подписка на команды: **QoS 0**.
- Ответ на `list_programs`: QoS 0, not retained.

### Anti-hang
- Вызывающий код гоняет `MQTTClient::loop()` кооперативно вместе с GSM; каждый `tick()` имеет байтовые лимиты RX/TX.
- Публикация статуса не блокируется бесконечно; если локальное измерение+стейджинг JSON заняло **> ~10 ms**, это **логируется**, соединение **не разрывается** автоматически из-за этого порога.

### Reconnect и 2-phase connect (SIM800)
- Сначала поднимается TCP (`Client.connected()` после `CONNECT OK` модема).
- Затем MQTT CONNECT только поверх живого TCP.

Протокол MQTT в CONNECT: при необходимости совместимости с брокером можно включить режим имени **`MQIsdp` / MQTT 3.1** через compile-time (см. `MQTTClient.Core.cpp`): `-DMQTT_VERSION=MQTT_VERSION_3_1`.

### Диагностика RX (SIM800)
После ошибок установления связи возможен диагностический HTTP GET на `1.1.1.1:80` (см. путь восстановления в GSM/MQTT-коде) для проверки входящего трафика.

## Память
- В callbacks нет динамических аллокаций под команды; размер входного JSON ограничен `JsonBytes::Mqtt::CMD_JSON_MAX`.
- JSON статуса собирается потоково в TX-буфер FSM (`publishPrintedMeasured`), без большого постоянного payload в `.bss`.
- `MqttFsmClient::TX_MAX` / `STATUS_PAYLOAD_MAX_BYTES` — caps для wire; на ESP32-C3 подняты относительно ESP8266-эры.

## Сообщения

### 1) Command (вход, `mqtt.cmd_topic`)
Команды — **JSON-объект** (строка UTF-8). Поддерживаются:
- `{"action":"run","program":<1..255>}` — запуск программы по id.
- `{"action":"run_program","program":<1..255>}` — то же (синоним для совместимости с документацией/интеграциями).
- `{"action":"list_programs"}` — запросить список программ.

Семантика:
- `run` / `run_program` → `AppPorts.control.startProgram(ctx, programId)` (см. `MQTTClient.Core.cpp`).
- `list_programs` → публикация в `mqtt.status_topic` (см. ниже).

### 2) Connection markers (выход, `mqtt.status_topic`)
На одном и том же топике, что и JSON, могут появляться **краткие не-JSON** сообщения для мониторинга связи:
- retained `"offline"` — через LWT при обрыве;
- retained `"online"` — после успешного CONNECT.

Подписчики, ожидающие только JSON, должны отличать payload по содержимому (или подписаться на несколько топиков, если измените конфигурацию).

### 3) StatusSnapshot (периодический JSON, выход, `mqtt.status_topic`)
Формируется `emitMqttStatusJson()` в `src/mqtt/MqttStatusBuilder.cpp` — поле **`schema` не используется**.

Минимальный перечень полей текущей прошивки:
| Поле | Тип | Смысл |
|------|-----|--------|
| `uptime` | number | секунды с апайма |
| `mode` | string | строка режима из снимка |
| `voltage` | number или `null` | напряжение, если известно |
| `tempSensors` | array | элементы `{ id, valid, lastMs, t }` (см. `MqttStatusBuilder.cpp`) |
| `relaysById` | object | ключ = id реле (строка-число), значение bool |
| `current_program` | number | только если выполняется программа |
| `last_program` | number | последняя активность |
| `runtime` | object | `inputTriggersById`, `tempTriggersById` — ключи id триггеров, значение bool |
| `inputsById` | object | ключ = id входа, значение bool |
| `last_error` | string | экранированная строка ошибки (может быть пустой) |

### 4) ProgramsList (ответ на `list_programs`, выход, `mqtt.status_topic`)
Объект вида:

```json
{"programs":[ … ]}
```

(сериализация из `Config::emitProgramListWrapped()` / `program_json::emitProgramListWrappedPrint`).
