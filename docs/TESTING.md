# Тесты

## Нативные (хост)

```bash
pio test -e native
```

Наборы Unity в `test/test_*/`. Производственные исходники перечислены в `platformio.ini`, секция `[env:native] build_src_filter`. Заглушки: `test/stubs/`. `test/native_pre.py` обходит пути MinGW; на Linux это пустая операция.

Покрытие: CI задаёт `PLATFORMIO_BUILD_FLAGS=--coverage`, затем `gcovr` по `src/` и `include/`. Локально: `pio test -e native`.

Хост дополнительно: `test_fs_atomic` (`closeWriteStream`: цель → `.bak`, затем `.tmp` → цель; `.bak` удаляется на общем пути, а `/programs/*.json` кроме `index.json` его оставляют; если rename `.tmp` не удался, `.bak` возвращается), `test_ota_stream` (белый список хвоста OTA и слот boot вокруг `Update.end` / `streamFinish`).

## JavaScript

Node 18+ (без npm). Загрузчик: `test/js/helpers/browser-env.mjs`.

```bash
node --test "test/js/**/*.test.mjs"
```

В том числе контрактные тесты: заводской `DEFAULT_CONFIG_JSON` против `settings.schema.json`, пути UI против схемы, действия программ против `ProgramAction.cpp`, эндпоинты `contract.js` против `server.on`.

## Прошивка

```bash
pio run -e esp32c3
python build.py release
python build.py fs
```

Снимок размера на момент записи этого абзаца, не норматив: `pio run -e esp32c3` давал RAM 58340 / 327680 (17.8%), Flash 1035108 / 1310720 (79.0%). Перед опорой на цифры прогон нужно повторить.

## Стенд (устройство)

- Закрытие TCP брокера при живом MQTT; входящий RING во время MQTT (CIP должен стечь, затем голос).
- MQTT `run`, пока программа выполняется: старый запуск вытесняется, его id получает финальный ответ. Во время `ENGINE_START` удачный `run` тоже забирает слот и снимает только крутящийся стартер.
- Стоп при уже включённых реле зажигания (UI или MQTT): `stop()` гасит все реле, не только стартер.
- Входящий DTMF во время `ENGINE_START` в том же тике вызывает `start()`: слот передаётся, стартер снимается, остальные реле остаются.
- ATH без OK до таймаута сброса срывает голосовую эпоху и не стартует программу по DTMF.
- ENGINE_START: по времени / rpm / входу; обрыв предохранителя стартера → `RELAY_TIMEOUT`; вход блокировки включён и выключен. Исчерпание попыток: все реле гаснут, `START_FAILED`, в журнале маска реле до выключения (`relays on=0x..`). Между попытками зажигание и обход остаются включёнными.
- Сохранение настроек с полями, скрытыми `showIf` (время прокрутки, параметры импульса, таймаут AP) — значения должны вернуться как были.
- Параллельные POST и обрыв посередине загрузки; нет LittleFS → FallbackWebPage. Поток OTA на хосте (`test_ota_stream`): белый список хвоста (`/index.html.gz`, `/validation/settings.schema.json`) принимается, `/config.json` и `/programs/1.json` отклоняются.
- Хвост LittleFS после `Update.end`: запись хвоста идёт при закрытой сессии Update. Ошибка хвоста не вызывает `Update.abort`; boot возвращается на выполняемый образ. Хост (`test_ota_stream`): после `Update.end` и до успешного `streamFinish` boot — выполняемый слот, после успеха — новый; сбой хвоста не зовёт `Update.abort`, boot остаётся на выполняемом. Запись на реальной флеш — на стенде.
- UI: тост, баннер аппаратной ошибки, неизвестный шаг программы отклоняется с `PROGRAM_INVALID_REF`.

Полевой лист: [`modules/gsm_mqtt_field_log.md`](modules/gsm_mqtt_field_log.md).
