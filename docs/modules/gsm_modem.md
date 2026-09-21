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
- `ERROR` — восстановление с эскалацией (см. ниже)

### 2а) UART: гипотеза скорости и поиск baud

Параметры и таймеры задаются только в [`include/common/Constants.h`](../../include/common/Constants.h), `namespace GSM` (например `UART_BAUD`, `BAUD_FALLBACK_AFTER_MS`, `BAUD_SEARCH_ROUND_COOLDOWN_MS`, `MODEM_ID_VERIFY_TIMEOUT_MS`, подстрока `MODEM_VERIFY_MANUFACTURER_SUBSTR` для `AT+CGMI`).

Краткая политика:

- В `begin()` MCU сразу открывает **`GSM::UART_BAUD`**, без синхронного «autobaud-скана» в `begin()` (единичные шаги — в `handleInit()` / раундах).
- Первый живой канал считается подтверждённым только после пары **`AT` → OK** и **`AT+CGMI`** с ожидаемым производителем (`SIMCOM` по умолчанию) и финальным `OK`.
- Если за **`BAUD_FALLBACK_AFTER_MS`** с момента первой отправки `AT` на целевой скорости подтверждения всё ещё нет, включается **асинхронный поиск baud** по небольшой таблице кандидатов; между полными неудачными проходами — **`BAUD_SEARCH_ROUND_COOLDOWN_MS`**, один содержательный шаг за тик (`update`).
- После успешного подтверждения выполняются **`AT+IPR?`**, при расхождении с текущей рабочей скоростью — **`AT+IPR=`** и **`AT&W`** (NV модема), **без** новых ключей в `config.json`; утечку спама `AT+IPR?` в одном цикле GSM снижает бит-сессии в контроллере.
- Успешное подтверждение в рамках сессии **липнет** до `GSMController::stop()` (`_verifiedModemContactSinceStop`): при восстановлении после `ERROR` без `stop()` поиск baud снова не включается, повтор сканирования — только после «чистого» старта.
- При старте раундов поиска в логе ожидается редкая строка вида `baud search round N start`; рутиновый **`AT+CFUN=1,1`** после каждого успешного канала не выполняется (CFUN остаётся в тяжёлом recovery, см. уровни ниже).

### 3) SIM800 TCP (URC-first)

- Завершение connect/close/send — по URC (`CONNECT OK` / `CONNECT FAIL`, `CLOSED`, `SEND OK` / `SEND FAIL`).
- RX по умолчанию: **`+IPD`** и `AT+CIPRXGET=0` (push mode).
- Payload внутри `+IPD` — **бинарный**; пока принимается тело IP-пакета, `ModemUart` переводится в `dataMode` (выключается line-framing для URC), чтобы сырой поток не ломал парсеры строк.

### 4) MQTT поверх `Client`

MQTT реализован **в прошивке** как неблокирующий `MqttFsmClient` (см. [`mqtt.md`](mqtt.md)), а не внешней библиотекой с долгими блокирующими `read()`.

Инвариант: любой код, который ждёт данные от модема, обязан регулярно прокачивать UART/URC и отдавать время сети:

- `ModemUart.pollRx()` (по контракту стека);
- `Sim800TcpTransport.tick(millis())`;
- при необходимости `yield()`.

Это обеспечивается через `PumpFn` в `Sim800ClientAdapter`.

## Recovery policy (уровни)

От дешёвого к дорогому:

- **L1**: TCP reconnect (повтор `CIPSTART`, backoff)
- **L2**: сброс IP-стека (`CIPSHUT`)
- **L3**: bearer reset (`SAPBR=0,1` → `SAPBR=1,1`)
- **L4**: модемный reset (`CFUN=1,1`)

При reattach действует backoff (ступени до ~180 с, зависимость от CSQ и пр.) — см. реализацию `GSMController`.

## Диагностика

### Политика логирования (SSE)

**Всегда в потоке** (нарратив «что делает система»): смены состояний FSM (`changeState`), `begin()`/`stop()`, успешное завершение INIT (`INIT OK -> …`), поиск baud (старт раунда / лимит раундов), ошибки AT и recovery (уровни L1–L4), reattach/backoff, решения пользователя (ребут модема), редкие события READY (PDP/TCP streak). Успешные промежуточные шаги bring-up (например только что выполнен SAPBR) **не** дублируются строками — их видно по переходу состояния.

**Тихий парсинг URC:** `+CREG`/`+CGATT` обновляют состояние без спама. **`+CSQ`** логируется только при заметном изменении RSSI (порог `GSM::URC_RSSI_LOG_DELTA` в [`Constants.h`](../../include/common/Constants.h)); **`+COPS`** — только при смене строки оператора.

**TCP (`Sim800Tcp`):** подключение/обрыв/ошибка accept — краткие фиксированные строки. Детали (**`+IPD`**, «CIPSTART accepted») включаются только при сборке с **`SERIAL_DEBUG`** или если **`Sim800Tcp::TCP_VERBOSE_LOG`** в `Constants.h` выставлен в `true`.

### Логи и Serial monitor

- Ключевые логи — редкие, событийные (смена состояния, итоги TCP).
- Если модем делит **UART0 (`Serial`)** с USB-UART адаптером, в монитор могут попадать произвольные байты MQTT (`CIPSEND`), не интерпретируйте их как текстовые логи прошивки.

Предпочтительно: прикладной лог через UI/SSE; низкий уровень — структурные сообщения вида `[Sim800Tcp]`, `[GSMController]`.

### Анти-шторм в reconnect сценариях

- Повторы `MQTT connect fails -> GSM reattach` должны быть rate-limited (не каждый тик цикла).
- High-frequency RX snippets (`GSMController` ring tail) выводятся в `logSerialOnly`, а не в SSE, чтобы не забивать очередь `/events`.
- На ключевых событиях деградации (`connect fails` порог, вход/выход OTA pressure окна) снимаются heap-метрики через `Core::logHeapSnapshot`.

### Сценарии проверки

**1) HTTP RX sanity**

- Цель: убедиться, что входящие байты проходят независимо от MQTT.
- Поднять GSM до `READY`, выполнить one-shot TCP (HTTP GET на `1.1.1.1:80`).
- Ожидание: в логе теста `bytes > 0` или подтверждённое чтение через `CIPRXGET`.

**2) MQTT handshake**

- TCP даёт `CONNECT OK`, затем успешный MQTT CONNECT (`CONNACK`).
- Ожидание логов: строка вида `[MQTTClient] Connected. subscribe=...` после подписки.
- После успешного MQTT-сессии клиент может опубликовать retained `"online"` на `mqtt.status_topic` (см. [`mqtt.md`](mqtt.md)).

**3) UI при SoftAP**

- SoftAP UI и GSM/MQTT сосуществуют на ESP32-C3.
- Cellular suspend только при SoftAP down (NORMAL) или OTA upload pressure.
- Ожидание: в панели виден живой `gsmState` во время загрузки UI (без `/ui/ready`).
