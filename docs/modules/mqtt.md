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

**Нет silent drop (при живой сессии):** на кадр с валидным `id` на `{prefix}/cmd` устройство всегда пытается отдать ≥1 `/reply` с тем же `id`. Если Ctrl/pending заняты и 202 не удалось stage — команда **не** ставится в inbound (клиент может повторить). Pending replies **не** вытесняются silently.

**Исключения:**
- oversized payload без извлекаемого `id`;
- **разрыв сессии** (leave READY / `disconnect` / `tcp_drop`): inbound и pending void — клиент опирается на LWT/`avail` и таймаут.

### Транспорт (SIM800)
- Один MQTT-кадр = один атомарный `AT+CIPSEND` (`write` только буферизует, `flush`/`flushSend` стартует send).
- RX ring overflow → reconnect (не drop-oldest — иначе desync framing).
- `+IPD` framing exclusive while matching/reading; last payload byte never falls through to raw sniffer; `CIPHEAD=1` required.
- MQTT RX stall: валидный неполный кадр → fail-closed reconnect (`rx_incomplete`); junk head → byte-hunt. Лог: hex головы + transport forensic (см. `gsm_modem.md`).
- Tele не стартует CIPSEND, пока inbound assemble ждёт хвост кадра (снижает truncation mid-send).
- `mqtt.loop()` always when READY; mid-CIPSEND TX no-op, RX drained.
- Keepalive dead-man: no MQTT RX for `1.5 × keepAliveSec` → `keepalive_timeout` Error → reconnect.
- TCP drop / SEND FAIL path → `tcp_drop` Error (counts toward GSM reattach streak ≥3).
- Ctrl serialize: ack/final wait MQTT Ctrl queue and `gsm.tcpBusBusy()`.
- PUBLISH not on `/cmd` — ignore (no reconnect).

### Доставка
- LWT `/avail` `"offline"` retained Will QoS1; после connect — `"online"` **только после SUBACK** (пока `/cmd` не подтверждён — `avail` не online; SUBSCRIBE ретраится по timeout).
- `/status` QoS0, период `publish_interval_sec` (если Tele-idle); первый Tele после connect — full, далее delta или skip по significant changes.
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
{"id":"t1","cmd":"set_time","epoch":1710000000}
```

| Поле | Правило |
|------|---------|
| `id` | 1..`MqttCmd::ID_MAX_LEN` (16) |
| `cmd` | `run` \| `stop` \| `list` \| `status` \| `set` \| `set_time` |
| `program` / `name` / `ref` / `enabled` / `epoch` | по cmd (`set_time` требует `epoch` ≥ 1700000000) |

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

`emitMqttStatusJson` / `emitMqttStatusDeltaJson`. QoS 0, не retained. Wire всегда **компактный** JSON (без pretty-print). Лимит тела `JsonBytes::Mqtt::STATUS_PAYLOAD_MAX_BYTES` (900).

**Каналы:** Tele периодический; `cmd=status` → fat `/reply` с `"status":{…}` всегда **full** (Tele не форсируется). Первый Tele после connect — full; далее delta или skip.

**Full / delta / skip:** `forceFull` или нет baseline → full; иначе undelivered `diag.last_err` → TX; иначе significant → delta; иначе skip. Тик `uptime` / `time.epoch` сами по себе skip не отменяют.

**Вложенная схема (breaking vs legacy flat):** корень `full`, `uptime`, `mode`; объекты `time`, `hw`, `radio`, `program`, `runtime`, `diag`.

#### Full (`"full":true`)

Все объекты присутствуют. `program.current` всегда есть (`null` если не running). `diag.last_err` опускается, если очередь пуста.

```json
{
  "full": true,
  "uptime": 3600,
  "mode": "AUTO",
  "time": {
    "epoch": 1710000000,
    "synced": true,
    "stale": false,
    "tzOffsetHours": 3,
    "timeSource": "NTP"
  },
  "hw": {
    "voltage": 12.4,
    "engineRunning": false,
    "inputsById": {"1": false, "2": true},
    "relaysById": {"1": false, "2": false},
    "tempSensorsById": {
      "10": {"valid": true, "lastMs": 1234, "t": 21.5}
    }
  },
  "radio": {"csq": {"rssi": 20, "ber": 0}},
  "program": {
    "running": false,
    "current": null,
    "last": 3,
    "timerRemaining": 0
  },
  "runtime": {
    "thermostat": false,
    "batterySaver": true,
    "inputTriggersById": {"1": false},
    "tempTriggersById": {"2": true}
  },
  "diag": {
    "freeHeap": 45000,
    "last_err": [
      {"code": 21, "msg": "Panic reset", "active": true}
    ]
  }
}
```

| Объект / поле | Смысл |
|---------------|--------|
| `time.stale` | время старше ~48 h (даже при `synced`) |
| `hw.tempSensorsById` | только смапленные id≠0; `t` null если !valid |
| `radio.csq.rssi` | 0..31; `99` unknown |
| `radio.csq.ber` | 0..7; `-1` never seen |
| `program.timerRemaining` | секунды |
| `diag.freeHeap` | байты |
| `diag.last_err` | one-shot undelivered (newest first); omit if empty |

#### Delta (`"full":false`)

Всегда: `full`, `uptime`. Остальное — partial deep-merge:

- `mode` — если сменился
- `time` — **весь** объект при смене `synced`/`stale`/`tzOffsetHours`/`timeSource` (не один тик `epoch`)
- `hw` — partial: voltage (ε 0.05), engine, partial maps inputs/relays/temps (temp: id/valid/t ε 0.1; не lastMs alone; id→0 не шлём)
- `radio` — если csq изменился
- `program` — partial: `running`, `current` (id или `null` при стопе), `last`, `timerRemaining` (`|Δ|≥1` с или смена running)
- `runtime` — partial thermostat/batterySaver + changed trigger ids (без tombstone удалённых config id)
- `diag` — `freeHeap` при `|Δ|≥2048`; `last_err` полный массив если count>0

#### Significant → delta (иначе skip)

`mode`; `time` meta (не epoch alone); `hw` voltage(ε)/engine/inputs/relays/temps; `radio.csq`; `program` running/current/last/timer; `runtime` thermostat/batterySaver/triggers; `diag.freeHeap` ≥2 KiB; `diag.last_err` changed **или** count>0.

**Не significant:** тик `uptime`, тик `time.epoch`, только `temp.lastMs`, подпороговые Δ voltage/temp/heap/timer.

#### Merge для клиента

1. `full:true` → заменить state.
2. `full:false` → deep-merge; `program.current:null` = не running; maps merge по ключам.
3. После reconnect / смены base config → full (`cmd=status` или first Tele).
4. Отсутствие `diag.last_err` ≠ «ошибок нет навсегда» (one-shot undelivered).

#### `diag.last_err` delivery

```json
"last_err":[
  {"code":21,"msg":"Panic reset","active":true},
  {"code":48,"msg":"MQTT connection failed","active":false}
]
```

| Field | Meaning |
|-------|---------|
| `code` | `ErrorCode` as uint8 |
| `msg` | Stable short string (`errorCodeToString`) |
| `active` | Still current uncleared error |

**GSM sticky codes:** `GSM_NO_RESPONSE` / `GSM_REG_FAIL` / `GSM_APN_FAIL` — `active` cleared on successful entry to READY (history kept for one-shot delivery). Bearer/SAPBR exhaustion sets `GSM_REG_FAIL` when deregistered or CSQ is 99/`<=1`, otherwise `GSM_APN_FAIL`.

**Delivery:**

1. Boot/RTC undelivered → first `full` after MQTT connect
2. New runtime errors → once on next periodic status
3. Mark delivered only after Tele left the MQTT TX queue **and** modem CIPSEND epoch ended (`!tcpBusBusy`); on `tcp_drop`/disconnect — abandon and retry next status

SoftAP SSE `gsm` event mirrors `gsmState`, `csq`, and `mqttConnected` for local debug (`gsmState` не в MQTT Tele — канал жив только при рабочем контуре модема).

**Time vs MQTT CIP:** modem cascade (CCLK/CIPGSMLOC/CNTP) cannot share the IP stack with MQTT TCP. Boot still runs cascade before first CIPSTART. On `sync_interval`, TimeSyncManager may briefly drain MQTT (offline + DISCONNECT + CIPCLOSE), run the same full cascade, then reconnect — same drain recipe as voice/SMS. Manual `set_time` still applies without tearing CIP. `time.stale` (48 h) is reported in status; schedule triggers still use `isSynced()`.

**Abnormal reboot fact** (always undelivered on boot, except clean reasons):

| `esp_reset_reason` | `code` / msg |
|--------------------|--------------|
| WDT / task WDT / int WDT | Watchdog reset |
| PANIC | Panic reset |
| BROWNOUT | Brownout reset |
| EXT / UNKNOWN / SDIO / other | Unexpected reset |
| POWERON / SW / DEEPSLEEP | not recorded (clean power / OTA / `/reboot`) |

Clean soft reboot: LWT offline→online + `uptime` reset only. Power-cut may clear RTC → no `diag.last_err`.

**Не в MQTT status:** `gsmState`, `mqttConnected`, имена программ, `flashCommit`, frequencies, sticky `lastError`.

### Вне scope
QoS1, текстовые `err` на wire, persist LRU, ACL, legacy API.
