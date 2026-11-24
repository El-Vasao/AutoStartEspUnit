# Архитектура и правила модульности (ESP32-C3)

Этот документ фиксирует *контракты и ограничения* для кодовой базы. Он нужен, чтобы рефактор не превращался в набор
разрозненных правок и чтобы новые изменения не размывали границы модулей.

Цель: окружение PlatformIO `esp32c3` (около 170 КБ свободной кучи после загрузки). Пины: [`HARDWARE.md`](HARDWARE.md).

## 1) Слои и направление зависимостей

Цель — чтобы зависимости шли “сверху вниз”, а UI/transport не протекали в доменную логику.

Рекомендуемая схема:

```mermaid
flowchart TD
  App[src/main.cpp] --> Core[core/Core]
  Core --> Domain[io,program,core/*Manager]
  Core --> Services[config,fs,gsm,mqtt,ota]
  Core --> Web[web/WebServer]
  Services --> FS[fs/FsManager]
  Web --> Core
  Web --> FS
```

Разрешённые направления (упрощённо):
- **`core` →** любые подсистемы (orchestration).
- **`core/*Manager`, `io`, `program` →** `config` (чтение настроек) и друг к другу по необходимости.
- **`config` →** `fs` (чтение/запись файлов), но не наоборот.
- **`web` →** `core`, `config`, `fs` (через узкие контракты); **запрещено** тянуть web в `io/program/core-managers`.

### “Точка правды” для цикла
Весь прогресс — в одном контролируемом месте:
- `setup()` вызывает `core.begin()`;
- `loop()` вызывает `core.update()` и больше ничего.

Так проще гарантировать:
- регулярное обслуживание watchdog / `Core::cooperate()`;
- fairness FreeRTOS + lwIP при длинных FS/OTA циклах;
- отсутствие пересечения flash-операций с логикой выполнения программ.

## 2) Публичное и внутреннее (границы заголовков)

Правило: **публичная поверхность модуля — только `include/<domain>/*.h`.**

### Публичные заголовки (`include/**`)
Должны:
- описывать контракт модуля: что гарантирует, что требует, какие инварианты;
- по возможности избегать “тяжёлых” include’ов (forward declarations, перенос реализации в `.cpp`);
- не включать “внутренние” заголовки других подсистем.

### Внутренние заголовки (`src/<domain>/internal/**`)
- являются частью реализации;
- **не включаются вне `src/<domain>/**`**;
- могут включать тяжёлые зависимости и детали, необходимые только реализации.

Практический приём:
- public header даёт минимальный API;
- `.cpp` и `internal` хедеры включают всё остальное.

## 3) Политика работы с памятью (ESP32-C3)

Предсказуемый профиль RAM важен (OTA `Update.begin`, SoftAP+SSE+GSM; около 170 КБ свободной кучи после загрузки).

### Оборот кучи / `String`
`String` допускается только там, где API Arduino/ESPAsyncWebServer принуждает к этому.

Запрещено:
- хранить `String` в полях “живущих долго” объектов (если это не строго обосновано);
- делать `String`-конкатенации в циклах и hot-path;
- превращать `String` в “универсальный буфер”.

Разрешено:
- краткоживущие `const String&` из параметров HTTP/FS Dir API с немедленным разбором в числовые/`char[]`.

### JSON
Для web/api/sse и MQTT:
- JSON эмитится в фиксированные/`char[]` буферы или потоково в Print (`sendJsonBuffered`, SAX parser);
- ёмкости/лимиты фиксируем в `include/common/Constants.h` (`JsonBytes::*`).

Дополнение:
- Для рантайм-исполнения программ — компактные шаги `CompiledStep`. Строка действия в рантайме не хранится; текст SMS лежит в фиксированном `message[]`.
- Эндпоинт `/bootstrap` — один buffered JSON (inventory + `live`).
- MQTT: топики `online`/`state`/`programs`/`hwMap`/`trigMap`/`cmd`/`reply`; payload статуса ≤900; кадр ≤ `MQTT_CIPSEND_MAX` (1600), слот `TX_MAX` 3072. Birth: каталоги → full retained `state` → `online=true`. Период = full retain; край = delta. Норматив — `docs/modules/mqtt.md`.

## 3.0) Контракт MQTT (топики, сообщения, доставка)
MQTT чувствителен к стабильности цикла (SIM800 + SoftAP).

Фиксируем контракт:
- **Идентификация устройства только по топику**: никаких `deviceId`/`unitId` в payload.
- **Топики**: пользователь задаёт только `BaseConfig.mqtt.topic_prefix`; суффиксы фиксированы:
  `/online`, `/state`, `/programs`, `/hwMap`, `/trigMap`, `/cmd`, `/reply`.
- **Delivery по умолчанию**:
  - LWT `false` на `{prefix}/online`: QoS1 + retained
  - Birth: три retained-хэша каталогов → full retained `state` → один `online=true` (без keepalive-повтора)
  - Период/`status`: full retained `state`; край ~250 мс: delta без retain; каталоги retain = 8 hex (см. `docs/modules/mqtt.md`)
  - Кадр ≤ `MQTT_CIPSEND_MAX` (1600)
- **Anti-hang**: чтение/запись режутся `MqttFsmClient::Budgets`. Локальное время encode+stage (~10 мс) только логируется.
  SIM800: send-epoch до `SEND OK`. `mqtt.loop()` на READY крутится и во время CIPSEND, кроме эпохи голоса/SMS после слива и тяжёлой синхронизации часов (см. [`modules/gsm.md`](modules/gsm.md)).
  MQTT outbound: Ctrl/Tele (`TX_Q_DEPTH=3`). Новый полный кадр заменяет неотправленный полный или дельту; каталог ничего не заменяет; телеметрия оставляет слот команде. Атомарный CIPSEND. RX overflow → reconnect.

### Долгие операции и watchdog/cooperate
Любые потенциально долгие операции (flash, большие сериализации, loop’ы по файлам):
- должны содержать “time slicing”: `Core::cooperate()` / `espHalFeedWdt()` (= `yield()`);
- не должны вызываться из enter/exit режимов без явного разбиения по времени.

## 3.3) Контракт жизни памяти по режимам

Контракт по памяти между `Core`, `WebServer`, `CellularCore` и `ModeManager`:

- `NORMAL` / `NORMAL_SILENT`:
  - GSM/MQTT обслуживаются штатно в обоих режимах (отличие silent — только SoftAP/веб off), SSE incremental в обычных soft queue.
- `OTA_UPDATE` (в т.ч. во время stream-upload):
  - программы stop, реле safe, triggers/battery saver/thermostat runtime off, cellular suspend;
  - **SSE остаётся** (логи/статус под обычными soft gates);
  - пакет кладётся в кольцо 4 КБ, `Update` и хвост LittleFS пишет основной цикл, без полного `/update.bin` на LittleFS;
  - HTTP отвечает `rebooting: true`, когда приём помечен финальным; смена слота и reboot — после успешного `streamFinish` на цикле;
  - неудачный `streamFinish` не перезагружает и возвращает режим, из которого вошли (`NORMAL`, `SETUP_AP` или `EMERGENCY_AP`). Из `NORMAL_SILENT` OTA не начинается.

Символы до OTA `otaUploadPressureActive` / `OTA_PREP_*` / `closeSseForOta` **удалены** (ESP32-C3).

Полевой baseline: [`HARDWARE.md`](HARDWARE.md). Детали stream OTA: [`modules/ota.md`](modules/ota.md).

## 3.1) “Горячие” и “холодные” зоны

**Hot-path** — всё, что выполняется часто и влияет на стабильность:
- `Core::update()` и handlers режимов
- `io/*::update()`
- `ProgramExecutor::update()`
- FSM GSM/MQTT service в NORMAL режимах
- web housekeeping (`WebServer::update()`) и периодическая отправка SSE статуса

Правило: в hot-path не добавляем новую динамику (`String`, большие временные JSON, malloc/free).

## 3.2) Сотовая связь (SIM800): контракты AT, транспорта и журнала

Поведение `begin()`/`INIT` описано в [`modules/gsm.md`](modules/gsm.md); константы — только из `namespace Gsm` в `Constants.h`.

### Единый AT pipeline
- **Все AT-команды должны идти через `AtSession` + `ModemUart`**.
- Причина: единый контроль таймаутов/очередей + WDT-safety (yield/time slicing).

### Транспорт TCP SIM800 (RX/TX)
- Connect/Close управляются URC (`CONNECT OK`, `CLOSED`, `SEND OK/FAIL`).
- RX стратегия по умолчанию: **push `+IPD`** (`AT+CIPRXGET=0`).
- Важно: payload `+IPD` — бинарный, line-framing должен быть отключён (`dataMode`) на время приёма payload.

### Уровни восстановления (предсказуемость)
При сбоях связь восстанавливаем по уровням (от дешёвого к дорогому):
- L1: restart GSM FSM (`begin` после cooldown)
- L2: reset IP stack: `CIPSHUT`
- L3: bearer reset: `SAPBR=0,1`
- L4: modem reset: `CFUN=1,1` (+ сброс sticky UART verify)
- На READY отдельно: TCP reconnect / reattach с backoff (MQTT + `handleReady`)

### Журнал SSE: политика обратного давления
- При активной UI-сессии возможен log-storm (GSM churn + transport polling).
- Политика: **лучше дропать логи через мягкую очередь SSE**, чем блокировать loop.
  Встроенного token-bucket throttle в `Logger` нет. Та же строка всегда печатается на UART1 TX (IO7).

## 3.4) Порог решения: замена сетевого стека

Полная замена `ESPAsyncWebServer` не делается “по умолчанию” при первом OOM.

Разрешение на миграцию даётся только если:
- OOM в `accept` повторяется после OTA pressure / SSE queue limits;
- команда согласовала риск регрессий API/SSE и длительный цикл валидации.

## 4) Политика доступа к LittleFS

Правило: **вся работа с FS только через `FsManager` (`fileSystem`)**.

Исключение:
- если сторонняя библиотека требует `FS&`, разрешено передать `fileSystem.webFs()`,
  но *не* использовать этот объект для произвольных `open/remove/exists` и т.п.

Причина:
- централизованный учёт ошибок и атомарность;
- единая политика deferred операций (чтобы не пересекать flash с выполнением программы).

`FsManager::gc()` на ESP32 Arduino **не уплотняет** том: освобождение известных осиротевших temp и обновление свободного места. После вызова всегда проверяйте `getFreeSpace()`.

## 5) Комментарии (стиль “осмысленной документации”)

Комментарии должны отвечать на вопросы:
- **контракт**: что гарантирует функция/класс, что требует, что не делает;
- **инварианты**: что должно оставаться истинным всегда (особенно в FSM и deferred-очередях);
- **ограничения платформы**: почему выбран такой подход (heap, SoftAP+GSM, LittleFS rename, SIM800).

Удаляем:
- дублирование очевидного;
- устаревшие/ложные утверждения про ESP8266 soft WDT / `LittleFS.gc()` / Logger throttle, если поведения уже нет.

## 6) Паттерн “узкий контракт” между модулями

Когда подсистеме A нужно знать что-то о подсистеме B:
- сначала пытаемся выразить это как **маленький контракт** (метод/тип/enum), а не как `#include` всего B в public header A;
- реализацию оставляем в `.cpp`, где можно подключить тяжёлые заголовки.

Цель:
- уменьшить compile fanout;
- сделать зависимости видимыми и устойчивыми (без “случайных” транзитивных include’ов).
