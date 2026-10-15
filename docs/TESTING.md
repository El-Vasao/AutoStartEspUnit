# TESTING.md

## Native (host)

```bash
pio test -e native
```

Unity suites under `test/test_*/`. Production sources listed in `platformio.ini` `[env:native] build_src_filter`. Stubs: `test/stubs/`. `test/native_pre.py` is a MinGW path workaround; on Linux it is a no-op.

Coverage: CI sets `PLATFORMIO_BUILD_FLAGS=--coverage` then `gcovr` over `src/` and `include/`. Locally: `pio test -e native`.

## JavaScript

Node 18+ (no npm). Loader: `test/js/helpers/browser-env.mjs`.

```bash
node --test "test/js/**/*.test.mjs"
```

Includes contract tests: factory `DEFAULT_CONFIG_JSON` vs `settings.schema.json`, UI paths vs schema, program actions vs `ProgramAction.cpp`, `contract.js` endpoints vs `server.on`.

## Firmware

```bash
pio run -e esp32c3
python build.py release
python build.py fs
```

Current `pio run -e esp32c3` size (this tree): RAM 58340 / 327680 (17.8%), Flash 1035108 / 1310720 (79.0%).

## Bench checklist (device)

- Broker TCP close while MQTT is up; inbound RING during MQTT (CIP should drain, then voice).
- ENGINE_START: timed / rpm / input; starter fuse cut → `RELAY_TIMEOUT`; block input enabled vs disabled.
- Save settings with fields hidden by `showIf` (crank time, pulse params, AP timeout) — values must round-trip.
- Parallel POSTs and mid-upload abort; OTA stream; missing LittleFS → FallbackWebPage.
- UI: toast, hardware error banner, program unknown-step rejected with `PROGRAM_INVALID_REF`.

Field log worksheet: [`modules/gsm_mqtt_field_log.md`](modules/gsm_mqtt_field_log.md).
