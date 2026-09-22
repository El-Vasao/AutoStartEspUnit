# `gsm` и `modem`: SIM800, FSM/URC, транспорт для MQTT

## Роль

Подсистема GSM поднимает сотовую связь и выдаёт сетевой `Client` для MQTT:

- управление модемом через AT (FSM и очередь команд);
- обработку URC;
- TCP transport поверх SIM800 в режиме `CIPMUX=0`.

Основные файлы:

- `include/gsm/GSMController.h` + `src/gsm/GSMController.*.cpp`
- `include/gsm/GsmModemStack.h` (композиция UART + AT session + TCP transport)
- `include/modem/*` + `src/modem/*` — `AtSession`, `ModemUart`, `Sim800TcpTransport`, адаптер `Sim800ClientAdapter : public Client`

## Контракт (инварианты)

### 1) Единый AT pipeline

- Все AT-команды идут через `AtSession` и `ModemUart`.
- Прямые `Serial.flush()` в прикладной логике не использовать: риск WDT и потери прогресса RX.

### 2) FSM GSM

`GSMController` работает как конечный автомат:

- `INIT` может перейти сразу в `REGISTERING`, `GPRS_SETUP` или `READY` после resume-опроса (сеть и bearer уже в рабочем состоянии), иначе — цепочка `REGISTERING` → `GPRS_SETUP` → … → `READY` как ниже.
- Типичный cold path после INIT: `REGISTERING` → `GPRS_SETUP` → `GPRS_ATTACH` → `GPRS_GETIP` → `READY`
- **Warm modem (ребут только ESP):** contact (`AT`/`AT+CGMI`) + status probe (`CREG`/`CGATT`/`SAPBR=2,1`) — без `CFUN`, без повторного open bearer если IP уже есть. `GPRS_ATTACH` тоже status-first: сначала `SAPBR=2,1`, open `SAPBR=1,1` только если IP нет; при ERROR open — повторный probe (часто bearer уже открыт).
- `ERROR` — восстановление с эскалацией (см. ниже)

### 2а) UART: гипотеза скорости и поиск baud

Параметры и таймеры задаются только в [`include/common/Constants.h`](../../include/common/Constants.h), `namespace GSM` (например `UART_BAUD`, `POST_BOOT_QUIET_MS`, `PRE_CFUN_AFTER_MS`, `POST_CFUN_QUIET_MS`, `POST_CFUN_RETRY_MS`, `BAUD_SEARCH_ROUND_COOLDOWN_MS`, `MODEM_ID_VERIFY_TIMEOUT_MS`, подстрока `MODEM_VERIFY_MANUFACTURER_SUBSTR` для `AT+CGMI`).

Краткая политика (cold-path):

1. В `begin()` MCU открывает UART на **`GSM::UART_BAUD`** с пинами `Pin::GSM_RX`/`GSM_TX` (через `ModemUart::begin`), без синхронного autobaud.
2. Quiet **`POST_BOOT_QUIET_MS`**, затем hypothesis: **`AT` → OK** и **`AT+CGMI`** с производителем (`SIMCOM` по умолчанию). Между неудачными hypothesis-попытками — **`HYP_RETRY_GAP_MS`**.
3. Если за **`PRE_CFUN_AFTER_MS`** контакта нет — один best-effort **`AT+CFUN=1,1`** на `UART_BAUD`, quiet **`POST_CFUN_QUIET_MS`**, повтор hypothesis (**`POST_CFUN_RETRY_MS`**). CFUN на чужом baud может не дойти — это ожидаемо.
4. Если после PreCfun контакта всё ещё нет — **асинхронный baud search** по таблице кандидатов (старт со следующего после `UART_BAUD`); между полными неудачными проходами — **`BAUD_SEARCH_ROUND_COOLDOWN_MS`**, один содержательный шаг за тик (`update`).
5. После успешного подтверждения — **`AT+IPR?`**, при расхождении с `UART_BAUD` — **`AT+IPR=`** и **`AT&W`** (NV), **без** ключей baud в `config.json`.

Дополнительно:

- Успешное подтверждение **липнет** до `stop()` (`_verifiedModemContactSinceStop`). После soft-reboot модема (PreCfun / UI / ERROR L4) sticky verify снимается (`gsmNoteModemSoftReboot`), чтобы PreCfun/search снова были возможны.
- Рутиновый CFUN после каждого успешного канала **не** выполняется; CFUN — PreCfun (один раз за цикл) и тяжёлый recovery L4 / UI reboot.

### 3) SIM800 TCP (URC-first)

- Завершение connect/close/send — по URC: точный `CLOSED`, `CONNECT OK` / `CONNECT FAIL`, `SEND OK` / `SEND FAIL`. Голый `ERROR` **не** рвёт TCP (его обрабатывает `AtSession`; иначе GSM AT на том же UART ломал бы сокет).
- **Send-epoch:** с enqueue `CIPSEND` до `SEND OK`/`SEND FAIL`/watchdog держатся `_sendInProgress` + `_modemTxLocked` (даже когда `AtSession` уже Idle после `>`). `CIPSTART` и GSM diag (`CSQ`/`COPS`) запрещены, пока `isBusBusy()`.
- Перед **первым** `CIPSTART` после `Sim800TcpTransport::reset()` — один `AT+CIPSHUT` (тёплый модем после ребута ESP). Cooldown `CONNECT_RETRY_COOLDOWN_MS` между попытками.
- `Client::connect` — **неблокирующий** kick `CIPSTART` (без wall-clock wait); иначе SoftAP/IWDT.
- Watchdogs: connect (`CONNECT_WATCHDOG_MS`) сбрасывает залипший `_connecting`; send (`SEND_WATCHDOG_MS`) после `CIPSEND`/`>` без `SEND OK` — end send-epoch + CIPSHUT recover.
- После `STACK_RECOVER_REATTACH_THRESHOLD` CIPSHUT-recover без успешного `CONNECT OK` — `needsBearerReattach` → GSM `requestReattach` (status-first `SAPBR=2,1`).
- Единый `takeResult`: TCP CIP* теги → `Sim800TcpTransport::consumeAtResult`, иначе GSM await absorb.
- RX по умолчанию: **`+IPD`** и `AT+CIPRXGET=0` (push mode).
- Payload внутри `+IPD` — **бинарный**; пока принимается тело IP-пакета, `ModemUart` переводится в `dataMode` (выключается line-framing для URC), чтобы сырой поток не ломал парсеры строк.
- **Selective discard во время CIPSEND:** сырой (non-`+IPD`) путь в MQTT RX дропается, пока `_sendInProgress || _modemTxLocked`; байты из framed `+IPD` ReadData **принимаются** (CONNACK/SUBACK/PINGRESP часто приходят mid-send). Post-send quiet не дропает payload — только откладывает MQTT parse через `shouldDeferMqttRead()`.

### 3а) CellularCore glue

- После первого `service()` — settle **`GSM::POST_BOOT_SETTLE_MS` (20 s)** до `gsm.begin()`.
- `mqtt.loop()` и reattach не вызываются, пока `gsm.tcpBusBusy()`.
- Hypothesis `AT`/`CGMI`: пауза **`HYP_RETRY_GAP_MS`** между повторами.

### 4) MQTT поверх `Client`

MQTT реализован **в прошивке** как неблокирующий `MqttFsmClient` (см. [`mqtt.md`](mqtt.md)), а не внешней библиотекой с долгими блокирующими `read()`.

Инвариант: любой код, который ждёт данные от модема, обязан регулярно прокачивать UART/URC и отдавать время сети:

- `ModemUart.pollRx()` (по контракту стека);
- `Sim800TcpTransport.tick(millis())`;
- при необходимости `yield()`.

Это обеспечивается через `PumpFn` в `Sim800ClientAdapter`.

## Recovery policy (уровни)

От дешёвого к дорогому в `handleError` (ERROR FSM):

- **L1**: restart FSM (`begin` после cooldown)
- **L2**: сброс IP-стека (`CIPSHUT`)
- **L3**: bearer reset (`SAPBR=0,1`)
- **L4**: модемный reset (`CFUN=1,1`), затем `gsmNoteModemSoftReboot`

На READY отдельно: TCP reconnect (MQTT), reattach с backoff (ступени до ~180 с, зависимость от CSQ) — см. `handleReady` / `CellularCore`.

## Диагностика

### Политика логирования (SSE)

**Всегда в потоке** (нарратив «что делает система»): смены состояний FSM (`changeState`), `begin()`/`stop()`, успешное завершение INIT (`INIT OK -> …`), поиск baud (старт раунда / лимит раундов), ошибки AT и recovery (уровни L1–L4), reattach/backoff, решения пользователя (ребут модема), редкие события READY (PDP/TCP streak). Успешные промежуточные шаги bring-up (например только что выполнен SAPBR) **не** дублируются строками — их видно по переходу состояния.

**Тихий парсинг URC:** `+CREG`/`+CGATT` обновляют состояние без спама. **`+CSQ`** логируется только при заметном изменении RSSI (порог `GSM::URC_RSSI_LOG_DELTA` в [`Constants.h`](../../include/common/Constants.h)); **`+COPS`** — только при смене строки оператора.

**TCP (`Sim800Tcp`):** подключение/обрыв/ошибка accept — краткие фиксированные строки (без периодического heartbeat `TCP: connected=1`). Детали (**`+IPD`**, «CIPSTART accepted») включаются только при сборке с **`SERIAL_DEBUG`** или если **`Sim800Tcp::TCP_VERBOSE_LOG`** в `Constants.h` выставлен в `true`.

### Логи и Serial monitor

- Ключевые логи — редкие, событийные (смена состояния, итоги TCP up/down).
- Если модем делит **UART0 (`Serial`)** с USB-UART адаптером, в монитор могут попадать произвольные байты MQTT (`CIPSEND`), не интерпретируйте их как текстовые логи прошивки.

Предпочтительно: прикладной лог через UI/SSE; низкий уровень — структурные сообщения вида `[Sim800Tcp]`, `[GSMController]`.

### Анти-шторм в reconnect сценариях

- Повторы `MQTT connect fails -> GSM reattach` должны быть rate-limited (не каждый тик цикла).
- High-frequency RX snippets (`GSMController` ring tail) выводятся в `logSerialOnly`, а не в SSE, чтобы не забивать очередь `/events`.
- Heap: периодический снимок не чаще `Timing::HEAP_SNAPSHOT_INTERVAL_MS` (5 мин) и только при сдвиге ≥ `HEAP_SNAPSHOT_DELTA_BYTES`; на деградации (`mqtt_connect_fail`, OTA/mode, `gsm_ready`) — tagged `Core::logHeapSnapshot`.
- MQTT исходящие: краткие `[MQTTClient] pub avail online` / `pub status full|delta bytes=N` / `pub reply bytes=N` (без PING/SEND OK spam).

### Сценарии проверки

**1) HTTP RX sanity**

- Цель: убедиться, что входящие байты проходят независимо от MQTT.
- Поднять GSM до `READY`, выполнить one-shot TCP (HTTP GET на `1.1.1.1:80`).
- Ожидание: в логе теста `bytes > 0` или подтверждённое чтение через `CIPRXGET`.

**2) MQTT handshake**

- После settle (`POST_BOOT_SETTLE_MS`) и GSM READY: TCP даёт `CONNECT OK`, затем успешный MQTT CONNECT (`CONNACK`).
- Нет шторма `CIPSTART` во время `CIPSEND` / между `>` и `SEND OK`.
- Ожидание логов: строка вида `[MQTTClient] Connected. subscribe=...` после подписки.
- После успешного MQTT-сессии: `[MQTTClient] pub avail online`, затем `pub status full …`, далее периодически delta/skip.

**3) UI при SoftAP**

- SoftAP UI и GSM/MQTT сосуществуют на ESP32-C3.
- Cellular suspend только при SoftAP down (NORMAL) или OTA upload pressure.
- Полный JSON status уходит без Instruction fault / IWDT.
- Ожидание: в панели виден живой `gsmState` во время загрузки UI (без `/ui/ready`).
