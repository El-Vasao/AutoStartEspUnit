# `mqtt`: MqttFsmClient, команды и статус

## Роль
MQTT-публикация статуса и подписка на команды выполняются **встроенным неблокирующим клиентом** (`MqttFsmClient`) через обёртку `MQTTClient`. Транспорт — `Client&` из GSM (SIM800 TCP).

Основные файлы:
- `include/mqtt/MQTTClient.h`, `src/mqtt/MQTTClient.Core.cpp`, `src/mqtt/MQTTClient.Commands.cpp`
- `include/mqtt/MqttFsmClient.h`, `src/mqtt/MqttFsmClient.cpp`
- парсинг команд: `include/mqtt/MqttCommandParser.h`, `src/mqtt/MqttCommandParser.cpp`
- JSON статуса: `src/mqtt/MqttStatusBuilder.cpp`

## Контракт
### Идентификация устройства
- Устройство **идентифицируется только по топикам**.
- В payload **нет** `deviceId`/`unitId` и т.п.
- Один device = один `topic_prefix`. Мультиклиент на один prefix — вне ответственности устройства.

### Топики (из `mqtt.topic_prefix`)
Пользователь настраивает только **`mqtt.topic_prefix`** (без trailing `/`), например `car/Subaru`.
Суффиксы фиксированы в прошивке (`MqttTopics` в `Constants.h`):

| Суффикс | Топик | Назначение |
|---------|-------|------------|
| `/avail` | `{prefix}/avail` | Presence: retained `online` / `offline` (LWT) |
| `/status` | `{prefix}/status` | JSON телеметрия |
| `/cmd` | `{prefix}/cmd` | Команды → device (QoS 0, не retained) |
| `/reply` | `{prefix}/reply` | Ответы / lifecycle ← device (QoS 0, не retained) |

Пустой `topic_prefix` → без LWT/online/subscribe/publish.

**Offline = нет команд:** QoS 0, `cleanSession=true`, `/cmd` без retain. Клиент шлёт `/cmd` только при `avail=online`. Устройство не копит отложенные команды.

### Доставка (политика по умолчанию)
Реализация: `MQTTClient.Core.cpp` + `MQTTClient.Commands.cpp` + CONNECT в `MqttFsmClient.cpp`.
- **LWT** на `{prefix}/avail`: текст `"offline"`, **retained**, **Will QoS = 1**.
- После **MQTT Connected**: SUBSCRIBE на `/cmd`, затем retained `"online"` (после SUBACK или таймаута ~8 с).
- Периодический JSON на `/status`: **QoS 0**, **без retained**, раз в `mqtt.publish_interval_sec` — дельта или skip; full после connect и по `cmd=status`.
- Подписка `/cmd` и ответы `/reply`: **QoS 0**.
- Clean disconnect: retained `"offline"` на `/avail` перед DISCONNECT.

### Anti-hang / очереди
- `CellularCore` не вызывает `mqtt.loop()`, пока `gsm.tcpBusBusy()`.
- Keepalive 30 с; `_lastTxMs` только после реальной UART-записи.
- Идемпотентность: LRU **16** слотов, TTL **30 мин** по полю `id` (fingerprint args + `ok`/`err` + для `run` последний `state`). Hit → replay без side-effect. Сброс LRU при MQTT session drop.
- Pending reply depth **2**; пока cmd обрабатывается — новые cmd → `err=busy` (или один входной буфер).
- RX MQTT: `RX_SIZE=512`; defer read только пока есть buffered TX.

### Reconnect (SIM800)
- 2-phase: TCP (`CONNECT OK`), затем MQTT CONNECT.
- После `"online"` — settle ~1.5 s до первого `/status`.
- Опционально MQTT 3.1 (`MQIsdp`): `-DMQTT_VERSION=MQTT_VERSION_3_1`.

### Логи (нарратив)
- Handshake: `connect` → TCP → CONNACK → subscribe → SUBACK → `pub avail online`.
- Cmd: `cmd recv` → `pub reply …`.
- Status: `pub status full|delta bytes=N`.

## Память
- `CMD_JSON_MAX` на вход; `LIST_PROGRAMS_JSON_MAX` (960) / `STATUS_PAYLOAD_MAX_BYTES` на выход.
- Outbound queue: `TX_Q_DEPTH = 3` × `TX_MAX = 1024`. Envelope+`programs` для `list` обязано влезать в MQTT TX.

## Сообщения

### 1) Command (вход, `{prefix}/cmd`)

Плоский JSON. Поле версии протокола **нет**. Legacy `action` / `run_program` / `list_programs` / `get_status` **не поддерживаются**.

| Поле | Правило |
|------|---------|
| `id` | обязательно, 1..16 символов (корреляция + идемпотентность) |
| `cmd` | `run` \| `stop` \| `list` \| `status` \| `set` |
| `program` | для `run`, 1..255 |
| `name` / `ref` / `enabled` | для `set` |

Примеры:

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
```

`set` → `name`: `thermostat` | `battery_saver` | `input` | `trigger` | `temp_trigger` (`ref` обязателен для input/trigger/temp_trigger).

Клиент: subscribe `/reply` до publish `/cmd`; таймаут reply ~10 с; ретрай с тем же `id`; новое действие → новый `id`. Даже если `avail` ещё online (окно Will ~45 с), отсутствие reply = недоставка.

### 2) Reply (выход, `{prefix}/reply`)

Timestamp в reply **не нужен**.

```json
{"id":"7f3a","cmd":"run","ok":true,"state":"accepted"}
{"id":"7f3a","cmd":"run","ok":true,"state":"finished"}
{"id":"7f3a","cmd":"run","ok":false,"err":"rejected","state":"failed"}
{"id":"7f3a","cmd":"stop","ok":true}
{"id":"7f3a","cmd":"set","ok":true}
{"id":"7f3a","cmd":"list","ok":true,"programs":[…]}
{"id":"7f3a","cmd":"status","ok":true}
```

`err`: `parse` · `unknown` · `args` · `not_found` · `rejected` · `busy` · `conflict`.

#### `run` / `stop` lifecycle
| state | Когда |
|-------|--------|
| `accepted` | успешный `startProgram` |
| `finished` | штатное завершение (`ProgramExecutor`) |
| `failed` | отказ start / авария / stop бегущей программы |

`stop` → `ProgramExecutor::stop()`; если программа не бежала — всё равно `ok:true`. Повтор `stop`/`run` с тем же `id` → replay без side-effect.

### 3) Connection markers (выход, `{prefix}/avail`)
- retained `"offline"` — LWT / clean disconnect;
- retained `"online"` — после CONNECT.

### 4) StatusSnapshot (JSON, выход, `{prefix}/status`)
Формируется `emitMqttStatusJson()` / `emitMqttStatusDeltaJson()`. QoS 0, **не** retained.

Каждое сообщение начинается с маркера **`full`**:
- `"full":true` — полный snapshot.
- `"full":false` — дельта (deep-merge по id).

Когда что шлётся:
- После connect (settle ~1.5 s): **full**.
- Периодика (`publish_interval_sec`): дельта при значимых изменениях, иначе skip.
- `cmd=status`: внеочередной **full** + thin reply `ok`.

Значимые изменения: `mode`, `engineRunning`, `last_error`, relays/inputs, triggers, program fields; `voltage` / temp — смена valid или \|Δ\| ≥ ε. Остановка программы в дельте: `"current_program":null`.

Порядок полей full-снимка (после `full`):

| # | Поле | Тип | Смысл |
|---|------|-----|--------|
| 1 | `full` | bool | `true` = snapshot, `false` = delta |
| 2 | `uptime` | number | секунды аптайма |
| 3 | `mode` | string | имя режима (до 15 символов) |
| 4 | `voltage` | number или `null` | напряжение |
| 5 | `engineRunning` | bool | `Core::isEngineRunning()` |
| 6 | `inputsById` | object | ключи `Pin::INPUT_IDS` |
| 7 | `relaysById` | object | ключи `Pin::RELAY_IDS` |
| 8 | `tempSensorsById` | object | `{ valid, lastMs, t }` по id сенсора |
| 9 | `current_program` | number | только если выполняется (в full); в дельте при stop — `null` |
| 10 | `last_program` | number | всегда в full |
| 11 | `runtime` | object | `inputTriggersById`, `tempTriggersById` |
| 12 | `last_error` | string | экранированная; пустая если нет ошибок |

Пример full:

```json
{
  "full": true,
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

Пример delta:

```json
{"full":false,"uptime":12600,"engineRunning":true,"relaysById":{"2002":true}}
```

Неймспейс id по умолчанию: входы `1xxx`, реле `2xxx`, сенсоры `3xxx`.

### Вне scope
QoS1 на cmd/reply, timestamp в reply, persist LRU across reboot, ACL, любое старое API / поле `v`.
