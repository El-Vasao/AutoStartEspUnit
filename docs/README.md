# Project docs

Source of truth for AutoStartEspUnit (ESP32-C3). See also root [README](../README.md).

## Start here
- Agent map (layout, commands, how to add a field/step/MQTT/SSE): [AGENTS.md](../AGENTS.md)
- Hardware / pins: [HARDWARE.md](HARDWARE.md)
- Build / flash / FS: [BUILD.md](BUILD.md)
- Architecture: [ARCHITECTURE.md](ARCHITECTURE.md)
- Frontend: [FRONTEND.md](FRONTEND.md)
- Tests: [TESTING.md](TESTING.md)
- Product invariants: [PRODUCT.md](PRODUCT.md)
- Conventions: [CONVENTIONS.md](CONVENTIONS.md)

## Modules

One file per `src/<mod>/` directory:

- [modules/common.md](modules/common.md)
- [modules/config.md](modules/config.md)
- [modules/core.md](modules/core.md)
- [modules/fs.md](modules/fs.md)
- [modules/gsm.md](modules/gsm.md)
- [modules/io.md](modules/io.md)
- [modules/json.md](modules/json.md) — JSON helpers live under `src/json/`
- [modules/modem.md](modules/modem.md)
- [modules/mqtt.md](modules/mqtt.md)
- [modules/ota.md](modules/ota.md)
- [modules/program.md](modules/program.md)
- [modules/web.md](modules/web.md)

Field worksheet: [modules/gsm_mqtt_field_log.md](modules/gsm_mqtt_field_log.md).

## Отложено

1. LittleFS из колбэков async HTTP. Обработчики ESPAsyncWebServer трогают LittleFS из задачи async TCP. Нужно только копировать байты в RAM и фиксировать через `FlashCommitCoordinator::tick()` в задаче цикла. Файлы: `src/web/WebServer.JsonIngest.cpp`, `src/web/WebServer.RoutesApi.cpp`.

2. `Update.end` после записи хвоста ФС. Не переносить `Update.end` на `streamFinish()`: сессия Update и LittleFS делят flash. Текущая схема: `end` после байтов прошивки, затем запись хвоста UI; при ошибке хвоста восстанавливать загрузочный раздел. Не откладывать задачу «просто перенести `Update.end`».
