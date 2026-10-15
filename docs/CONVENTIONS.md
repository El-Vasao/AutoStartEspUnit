# Конвенции проекта

Правила обязательны для нового кода и для правок существующего. Если правило мешает, его меняют здесь, а не обходят молча.

## Каталоги и модули

- Каталог — это модуль: `include/<mod>/` для публичных заголовков и `src/<mod>/` для реализации. Модули: `app`, `common`, `config`, `core`, `fs`, `gsm`, `io`, `json`, `modem`, `mqtt`, `ota`, `program`, `web`.
- Приватные заголовки модуля лежат только в `src/<mod>/internal/` и подключаются как `"<mod>/internal/<Name>.h"` (каталог `src/` есть в путях поиска). Из других модулей их не подключают.
- В `include/` нет каталогов `internal/`.
- Каждому модулю соответствует документ `docs/modules/<mod>.md`.

## Имена файлов C++

- Файл называется по главному типу, который в нём объявлен или реализован: `include/io/RelayController.h`, `src/io/RelayController.cpp`.
- Тип целиком в одном файле реализации — `<Type>.cpp`, без суффиксов.
- Тип разнесён по нескольким файлам — `<Type>.Core.cpp` (конструктор, `begin`, `update`) и `<Type>.<Part>.cpp` для остальных частей: `FsManager.Atomic.cpp`, `WebServer.RoutesApi.cpp`. Допускается ещё один уровень: `GsmController.Fsm.Gprs.cpp`.
- До первой точки всегда стоит имя типа, а не каталога. `src/core/ErrorManager.cpp`, а не `Core.ErrorManager.cpp`. `Core.Modes.cpp` правильно: это часть класса `Core`.
- Часть типа может реализовывать его приватный помощник: `WebServer.Runtime.cpp` содержит `WebServerRuntime`.
- Модуль свободных функций называется по пространству имён в PascalCase: `VehicleRules.cpp` ↔ `vehicle_rules`, `GsmAtParse.cpp` ↔ `gsm_at`, `StarterMachine.cpp` ↔ `starter::Machine`.
- Не создаются файлы-трамплины, которые только пробрасывают вызов в другой класс.

## Имена в коде

- Типы, `enum class`, структуры — PascalCase. Аббревиатуры пишутся как слова: `GsmController`, `MqttClient`, `OtaHandler`, `FsManager`, `SseStatusPort`, `RpmEstimator`. Исключение — обозначения деталей: `DS18B20`.
- Значения новых `enum class` — PascalCase (`starter::Phase::Cranking`). Перечисления в UPPER_SNAKE (`ErrorCode`, `CoreMode`, `GsmState`, `ActionId`) не переименовываются: их имена видны в JSON, UI и журналах.
- Функции и методы — camelCase. Поля класса — `_camelCase`. Приватные вспомогательные методы могут оканчиваться на `_` (`endSendEpoch_`), как уже принято в `gsm`, `modem` и `mqtt`.
- Глобальные экземпляры — camelCase: `logger`, `config`, `core`, `fileSystem`, `webServer`, `flashCommit`. Новые глобальные экземпляры не добавляются без необходимости: зависимости передаются через конструктор или `AppPorts`.
- Пространства имён с константами — PascalCase, все константы собраны в `include/common/Constants.h`: `Timing::`, `Limits::`, `Gsm::`, `Ota::`, `Adc::`. Пространства имён с функциями — snake_case: `vehicle_rules`, `gsm_at`, `config_internal`.
- Константы — `constexpr` в UPPER_SNAKE внутри своего пространства имён в `Constants.h`. Числа с единицами измерения называются с суффиксом единицы: `_MS`, `_SEC`, `_BYTES`, `_V`.
- Вспомогательные функции одного файла — в анонимном пространстве имён.

## Заголовки и комментарии

- `#pragma once` во всех заголовках.
- Первая строка вида `// include/io/RelayController.h` необязательна, но если есть, должна совпадать с путём файла.
- Публичный заголовок начинается с абзаца о назначении типа и ссылки на документ модуля.
- Язык комментария — язык окружающего файла (русский или английский). Комментарий объясняет ограничение или причину, а не пересказывает код.

## Журнал

- Весь диагностический вывод идёт через `logger.log(LogModule::<Модуль>, "...\n", ...)` из `include/common/Logger.h`. Вызов без модуля не компилируется. Прямой `Serial.print`/`printf` в прикладном коде запрещён; исключение — байтовый обмен с модемом в `ModemUart`.
- Формат строки задаёт логгер: `[<millis>ms] [<Модуль>] <текст>`. Сам текст префикса `[...]` не содержит.
- Модуль берётся из реестра `include/common/LogModule.h` и совпадает с именем типа, который пишет строку. Новый тип с журналом — новое значение в реестре.
- Два канала названы не по типу:
  - `AT` — сырой обмен с модемом. Интерфейс по подстроке `] [AT] ` отправляет такие строки в AT-консоль (`data/js/app/sse.js`), поэтому имя менять нельзя;
  - `Starter` — шаг ENGINE_START, чтобы журнал попыток запуска читался отдельно от остального исполнителя.
- Из других задач FreeRTOS (обработчики async_tcp веб-сервера, esp_timer) `logger.log` разрешён: строка ждёт в кольце на `Logging::DEFERRED_SLOTS` мест и уходит из основного цикла (`Core::update()` → `logger.flushDeferred()`). Потерянные при переполнении строки сообщаются строкой модуля `Logger`. Из обработчиков прерываний журнал не пишут: вызов игнорируется.
- `logger.logSerialOnly(...)` — шумные трассировки только в UART debug-сборки, в SSE не попадают.
- Длина строки — до `Logging::MAX_MESSAGE_LENGTH - 1` символов, длиннее обрезается.

Модули журнала и файлы, которые их используют:

- `AT`: `modem/AtSession.cpp`, `modem/ModemUart.cpp`
- `BatterySaverManager`: `core/BatterySaverManager.cpp`
- `CellularCore`: `core/CellularCore.cpp`
- `Config`: `config/Config.*.cpp`
- `Core`: `core/Core.Core.cpp`, `core/Core.Diagnostics.cpp`, `core/Core.Modes.cpp`
- `DigitalInputs`: `io/DigitalInputs.cpp`
- `ErrorManager`: `core/ErrorManager.cpp`
- `FlashCommitCoordinator`: `core/FlashCommitCoordinator.cpp`
- `FsManager`: `fs/FsManager.*.cpp`
- `GsmController`: `gsm/GsmController.*.cpp`
- `Logger`: `common/Logger.cpp` (потери отложенных строк)
- `ModeManager`: `core/ModeManager.cpp`
- `MqttClient`: `mqtt/MqttClient.*.cpp`
- `MqttFsmClient`: `mqtt/MqttFsmClient.cpp`
- `OtaHandler`: `ota/OtaHandler.*.cpp`
- `ProgramExecutor`: `program/ProgramExecutor.*.cpp`, `program/internal/ProgramExecutorInternal.h`
- `RelayController`: `io/RelayController.cpp`
- `ScheduleTriggerManager`: `core/ScheduleTriggerManager.cpp`
- `SensorsController`: `io/SensorsController.cpp`
- `Sim800TcpTransport`: `modem/Sim800TcpTransport.cpp`
- `Starter`: `program/ProgramExecutor.Starter.cpp`
- `ThermostatManager`: `core/ThermostatManager.cpp`
- `TimeSyncManager`: `core/TimeSyncManager.cpp`
- `TriggerManager`: `core/TriggerManager.cpp`
- `WebServer`: `web/WebServer.*.cpp`

## Ошибки

- Коды ошибок — только в `include/common/ErrorCodes.h`. Новый код получает строковое имя в `errorCodeToString` и описание в документе модуля, который его выставляет.

## JSON и внешние контракты

- Конфигурация (`/config.json`), программы (`/programs/*.json`) и MQTT-команды — ключи snake_case.
- Телеметрия (SSE-события и MQTT `status`) — ключи camelCase.
- Внутри одного документа стили не смешиваются. Изменение ключа — это изменение контракта: правятся прошивка, `data/validation/*.schema.json`, `data/ui/*.ui.json`, JS и документация одновременно.

## Фронтенд (`data/`)

- Файлы — kebab-case. Сторы Alpine — `data/js/app/stores/<область>-<имя>-store.js`, имя стора camelCase (`deviceStatus`, `uiToast`).
- Модуль — IIFE, который регистрирует себя в `window.APP.<раздел>`. Других глобальных имён модули не создают.
- Порядок склейки бандла задаёт список `JS_LIBS` в `build.py`. Новый файл без записи там в бандл не попадёт.
- UI-схемы — `data/ui/*.ui.json`, схемы валидации — `data/validation/*.schema.json`.

## Тесты

- Native-тесты (Unity): `test/test_<snake_case>/test_main.cpp`. Производственный файл, который нужен тесту, добавляется в `build_src_filter` секции `[env:native]` в `platformio.ini`.
- Заглушки Arduino и общие подмены — `test/stubs/`.
- JS-тесты: `test/js/<name>.test.mjs`, запуск `node --test "test/js/**/*.test.mjs"`. Вспомогательные модули — `test/js/helpers/`.

## Документация

- Корневой [`AGENTS.md`](../AGENTS.md) — карта для агентов: дерево `src/`, команды сборки и тестов, чеклист «как добавить» поле/шаг/MQTT/SSE. Не дублирует модульные docs.
- Документы верхнего уровня — `docs/UPPER_SNAKE.md` (`ARCHITECTURE.md`, `CONVENTIONS.md`, `BUILD.md`, `HARDWARE.md`, `FRONTEND.md`, `TESTING.md`, `PRODUCT.md`).
- Документы модулей — `docs/modules/<каталог src>.md` (включая `gsm.md` и `modem.md`).
- Неизвестный JSON на устройстве не конвертируется: пользователь пересоздаёт файл.
