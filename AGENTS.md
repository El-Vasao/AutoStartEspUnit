# AGENTS.md

ESP32-C3 firmware + Alpine.js SoftAP UI for vehicle remote auto-start (SIM800 MQTT, LittleFS, SSE). SoftAP is a trusted network: no API/OTA auth.

## Layout

- `include/<mod>/`, `src/<mod>/` — firmware modules (`common`, `config`, `core`, `fs`, `gsm`, `io`, `json`, `modem`, `mqtt`, `ota`, `program`, `web`)
- `data/` — UI sources; `build.py` concatenates `JS_LIBS` into `/app.js.gz`
- `test/test_*/` — Unity native; `test/js/` — Node `node --test "test/js/**/*.test.mjs"`
- `docs/` — architecture and module notes; conventions in `docs/CONVENTIONS.md`

## Commands

Windows PlatformIO is often `C:\Users\<you>\.platformio\penv\Scripts\pio.exe`.

```bash
python build.py release          # firmware + Version.h
python build.py fs               # gzip UI → LittleFS
pio run -e esp32c3
pio test -e native
node --test "test/js/**/*.test.mjs"
```

CI: `.github/workflows/ci.yml`.

## Rules

- Logger: `logger.log(LogModule::X, "...\n", ...)`. No `Serial.print` in app code.
- No heap `String` on hot paths. Time via `millis()` differences. Async HTTP handlers must not touch LittleFS.
- UART0 is shared by SIM800 and debug Serial.
- First-build: unknown JSON keys are ignored; files the firmware rejects are not converted. Soft parse of `/config.json` and `FallbackWebPage` stay.
- Names: `docs/CONVENTIONS.md`.

## How to add

**Config field:** `ConfigTypes.h` + `DefaultConfig.h` + `Config.BaseSax.cpp` + `Config.BaseJsonEmit.cpp` + `data/validation/settings.schema.json` + `data/ui/settings.ui.json`.

**Program step:** `ProgramAction.h/.cpp` + `ProgramExecutor.Execute.cpp` + `ProgramValidate.cpp` + `data/ui/program-steps.ui.json` + `data/validation/program.schema.json`.

**MQTT command:** `MqttCommand.h` + `MqttCommandParser.cpp` + `MqttClient.Commands.cpp` + `test/test_mqtt_command_parser`.

**SSE event:** `WebServer.SseBuilders.cpp` / `SseIncremental.cpp` + `data/js/app/sse.js` + `device-status-store.js`.

**Log module:** `include/common/LogModule.h`.

**Test:** `test/test_<name>/test_main.cpp` listed in `platformio.ini` `[env:native]` `build_src_filter`, or `test/js/<name>.test.mjs`.

## Done when

`pio run -e esp32c3`, `pio test -e native`, `node --test "test/js/**/*.test.mjs"` are green. UI changes that affect layout need a browser pass if tools exist.

## Bench (human)

See `docs/TESTING.md`: broker TCP drop, inbound call during MQTT, three start algorithms, fuse cut, block input, hidden settings fields, parallel POST, OTA, toasts.
