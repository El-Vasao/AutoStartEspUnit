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
- Прямые `Serial.flush()` в прикладной логике не использовать: риск WDT и потери прогресса RX. Для дождаться TX (смена baud / IPR NV) — `ModemUart::flushTx()`.

### 2) FSM GSM

`GSMController` работает как конечный автомат:

- `INIT` может перейти сразу в `REGISTERING`, `GPRS_SETUP` или `READY` после resume-опроса (сеть и bearer уже в рабочем состоянии), иначе — цепочка `REGISTERING` → `GPRS_SETUP` → … → `READY` как ниже.
- Типичный cold path после INIT: `REGISTERING` → `GPRS_SETUP` → `GPRS_ATTACH` → `GPRS_GETIP` → `READY`
- **Warm modem (ребут только ESP):** contact (`AT`/`AT+CGMI`) + status probe (`CREG`/`CGATT`/`SAPBR=2,1`) — без `CFUN`, без повторного open bearer если IP уже есть **и** `gsm.apn*` совпадает с последним GPRS_SETUP. Иначе — `GPRS_SETUP` (Contype/APN/USER/PWD). `GPRS_ATTACH` тоже status-first: сначала `SAPBR=2,1`, open `SAPBR=1,1` только если IP нет; при ERROR open — повторный probe (часто bearer уже открыт). Смена `apn*` на READY → reattach через `GPRS_SETUP`.
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
- **Send-epoch:** с enqueue `CIPSEND` до `SEND OK`/`SEND FAIL`/watchdog держатся `_sendInProgress` + `_modemTxLocked` (даже когда `AtSession` уже Idle после `>`). `CIPSTART` и GSM diag (`CSQ`/`COPS`) запрещены, пока `isTcpEpochBusy()` / сокет up. MQTT Ctrl/`last_err` confirm смотрят `tcpEpochBusy()` (не любой CSQ AT). Полный `isBusBusy()` (= epoch ∨ `AtSession::isBusy`) — для reattach/drain.
- Перед **первым** `CIPSTART` после `Sim800TcpTransport::reset()` — один `AT+CIPSHUT` (тёплый модем после ребута ESP). Cooldown `CONNECT_RETRY_COOLDOWN_MS` между попытками.
- `Client::connect` — **неблокирующий** kick `CIPSTART` (без wall-clock wait); иначе SoftAP/IWDT.
- Watchdogs: connect (`CONNECT_WATCHDOG_MS`) сбрасывает залипший `_connecting`; send (`SEND_WATCHDOG_MS`) после `CIPSEND`/`>` без `SEND OK` — end send-epoch + CIPSHUT recover.
- **`SEND FAIL` / CIPSEND accept fail / CIPSTART accept fail** — fail-closed: session down + CIPSHUT recover (как send WD). MQTT RX всегда drain (mid-CIPSEND +IPD безопасен; post-send quiet нет).
- После `STACK_RECOVER_REATTACH_THRESHOLD` CIPSHUT-recover без успешного `CONNECT OK` — `needsBearerReattach` → GSM `requestReattach` (status-first `SAPBR=2,1`).
- Единый owner `GSMController::drainAtResult_()` (только из `update` и cooperate-pump): `takeResult` → TCP CIP* `consumeAtResult`, иначе GSM await absorb. `Sim800TcpTransport::tick` **не** вызывает `takeResult` (иначе non-CIP OK съедался бы мимо GSM).
- RX: **`AT+CIPRXGET=0` → `AT+CIPHEAD=1` → `AT+CIPMUX=0`**. `CIPHEAD=1` обязателен: без него inbound TCP сырой и **дропается mid-CIPSEND** (`discardingTcpPayload_`) → обрезанный MQTT PUBLISH. Флаги ставятся только после **OK** на все три команды; `CIPSTART` — только после подтверждённого `CIPHEAD`. После `CIPSHUT` конфиг сбрасывается и переигрывается.
- **CIPSEND payload TX:** `ModemUart::writeBytes` пишет чанками по 32 байта с `pollRx()` между ними — иначе длинный status full (~400B) забивает HW RX FIFO и режет хвост `+IPD` (`rx_incomplete have≪need`).
- **CIPSEND vs +IPD:** `startSend_` не стартует, пока `_ipState != Idle`; MQTT Tele не flush'ится, пока в FSM собирается валидный неполный inbound кадр.
- Инвариант `onByte`: пока `_ipState != Idle`, raw CRLF-sniffer не трогает байт; после конца `+IPD` payload сбрасывается `_rawLineLen`; последний payload-байт всегда `return` (без fall-through).
- Payload внутри `+IPD` — **бинарный**; на время тела `ModemUart` в `dataMode` (без line-framing URC).
- **Selective discard mid-CIPSEND:** только non-`+IPD` путь; framed `ReadData` принимается (CONNACK/SUBACK/PINGRESP mid-send).
- При `rx_incomplete`: hex головы MQTT + `[Sim800Tcp] RX forensic` (`ipState`/`ipLen`/`ipRead`/send-epoch) + SoftAP UI count.

### 3а) CellularCore glue

- После первого `service()` — settle **`GSM::POST_BOOT_SETTLE_MS` (20 s)** до `gsm.begin()`.
- `mqtt.loop()` вызывается всегда при READY (mid-CIPSEND TX no-op, RX ring дренируется); reattach — по политике bearer, не блокируется только из‑за mid-send.
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
- **L2**: сброс IP-стека (`CIPSHUT`), wait bus idle
- **L3**: bearer reset (`SAPBR=0,1`), wait result
- **L4**: модемный reset (`CFUN=1,1`), затем `gsmNoteModemSoftReboot`

После `ERROR_RECOVERY_MAX_CYCLES` полных L1–L4 — sticky `GSM_NO_RESPONSE`, пауза `ERROR_RECOVERY_EXHAUSTED_MS`, затем ещё один медленный круг (или UI modem reboot). Счётчик циклов сбрасывается на READY.

На READY отдельно: TCP reconnect (MQTT), reattach с backoff (ступени до ~180 с, зависимость от CSQ) — см. `handleReady` / `CellularCore`. Backoff **не** сбрасывается на каждом входе в READY — только после стабильной MQTT-сессии (`clearReattachBackoff`). PDP DEACT / CLOSED-threshold идут через тот же `_reattachRequested` + cooldown. Перед уходом с READY: `tcp.stop` и ожидание idle AT bus; `CellularCore` дренирует MQTT как при `suspend`.

CSQ/COPS на READY — только при `!tcpSocketActive()` (не поверх живого MQTT CIP).

## Диагностика

### Политика логирования (SSE)

Каждая строка `logger.log` / `logSerialOnly` начинается с префикса **`[Nms]`** (`millis()`), затем тег модуля.

**Sinks:** debug (`SERIAL_DEBUG`) — SSE + UART; release — только SSE.

**Всегда в потоке** (нарратив «что делает система»): смены состояний FSM (`changeState`), `begin()`/`stop()`, успешное завершение INIT (`INIT OK -> …`), поиск baud (старт раунда / лимит раундов), ошибки AT и recovery (уровни L1–L4), reattach/backoff, решения пользователя (ребут модема), редкие события READY (PDP/TCP streak). Успешные промежуточные шаги bring-up (например только что выполнен SAPBR) **не** дублируются строками — их видно по переходу состояния.

**UART text mirror (`ModemUart`):** весь текстовый обмен MCU↔модем через `logger.log` — `[AT] >> cmd` на `writeLine`, `[AT] << line` на каждую framed RX-строку и lone `>`. Тело **`CIPSEND`** (`writeBytes`) и байты в **`dataMode` (`+IPD`)** не логируются. `AtSession` дополнительно пишет только `[AT] << TIMEOUT …` (ответа на проводе нет).

**Тихий парсинг URC:** `+CREG`/`+CGATT` обновляют состояние без спама. **`+CSQ`** логируется только при заметном изменении RSSI (порог `GSM::URC_RSSI_LOG_DELTA` в [`Constants.h`](../../include/common/Constants.h)); **`+COPS`** — только при смене строки оператора. (Сами URC-строки при этом уже видны в AT mirror.)

**TCP (`Sim800Tcp`):** подключение/обрыв/ошибка accept — краткие фиксированные строки (без периодического heartbeat `TCP: connected=1`). Детали (**`+IPD`**, «CIPSTART accepted») включаются только при сборке с **`SERIAL_DEBUG`** или если **`Sim800Tcp::TCP_VERBOSE_LOG`** в `Constants.h` выставлен в `true`.

### Логи и Serial monitor

- Ключевые логи — редкие, событийные (смена состояния, итоги TCP up/down), плюс AT wire mirror в SSE.
- Если модем делит **UART0 (`Serial`)** с USB-UART адаптером, в монитор могут попадать произвольные байты MQTT (`CIPSEND`), не интерпретируйте их как текстовые логи прошивки.

Предпочтительно: прикладной лог через UI/SSE; низкий уровень — структурные сообщения вида `[AT]`, `[Sim800Tcp]`, `[GSMController]`.

### Анти-шторм в reconnect сценариях

- Повторы `MQTT connect fails -> GSM reattach` должны быть rate-limited (не каждый тик цикла).
- High-frequency RX snippets (`GSMController` ring tail) — `logSerialOnly`. AT text mirror идёт в SSE; soft queue `SSE_SOFT_QUEUE_MAX=20` + hard `SSE_MAX_QUEUED_MESSAGES=21` (1 reserved): при переполнении soft копится счётчик, затем `[SSE] dropped N log messages` через reserved slot.
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
- Cellular suspend только в SETUP/EMERGENCY AP и `OTA_UPDATE` (не при SoftAP down в NORMAL).
- Полный JSON status уходит без Instruction fault / IWDT.
- Ожидание: в панели виден живой `gsmState` во время загрузки UI (без `/ui/ready`).

**4) Leave READY / reattach handoff**

- Симулировать MQTT fail streak ≥3 или PDP DEACT.
- Ожидание: `[Sim800Tcp] stop` / CIPCLOSE до SAPBR; `[Cellular] GSM left READY — draining MQTT`; нет AT queue storm.
- Повторные fail: backoff растёт (логи `Reattach … backoff=Ns`); после стабильного Connected backoff сбрасывается.

**5) last_err delivery**

- Ввести ошибку (например MQTT disconnect), дождаться status с `last_err`.
- Оборвать TCP до SEND OK: `last_err` должен появиться снова после reconnect (не «съеден» ранним markDelivered).

**6) cmd ack gate**

- При загруженном Ctrl: команда без успешного stage 202 не должна исполняться (`cmd not queued`).
- При живой сессии: `run` даёт 202 затем финал 200/500.

**7) Baud search ceiling**

- Модем на неверной скорости / без ответа: после `BAUD_SEARCH_MAX_PASSES` — ERROR + `GSM_NO_RESPONSE`, не бесконечный search.

Полевой журнал (pass/fail на железе): [`gsm_mqtt_field_log.md`](gsm_mqtt_field_log.md).
