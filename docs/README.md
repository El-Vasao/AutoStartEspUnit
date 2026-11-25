# Документация проекта

Источник правды по AutoStartEspUnit (ESP32-C3). Продуктовый обзор — в корневом [README](../README.md).

## Целевое железо

- Модуль MCU: **XH-C3F** на сетях ESP-12E (не Ai-Thinker C3-12F — другая разводка).
- SoftAP считается доверенной сетью: отдельной авторизации API/OTA нет.
- Пины и UART: [HARDWARE.md](HARDWARE.md).

## Быстрый старт для разработчика

```bash
python build.py release -u
python build.py fs
pio test -e native
node --test "test/js/**/*.test.mjs"
```

Подробности сборки и LittleFS: [BUILD.md](BUILD.md). Карта для агентов (раскладка, команды, как добавить поле / шаг / MQTT / SSE): [AGENTS.md](../AGENTS.md).

## С чего начать

- Железо и пины: [HARDWARE.md](HARDWARE.md)
- Сборка, прошивка, ФС: [BUILD.md](BUILD.md)
- Архитектура: [ARCHITECTURE.md](ARCHITECTURE.md)
- Фронтенд: [FRONTEND.md](FRONTEND.md)
- Тесты: [TESTING.md](TESTING.md)
- Стенд MQTT-аудит: [bench/](bench/README.md)
- Продуктовые инварианты: [PRODUCT.md](PRODUCT.md)
- Чем руководствуется прошивка: [BEHAVIOR.md](BEHAVIOR.md)
- Сценарии использования: [USAGE.md](USAGE.md)
- Конвенции: [CONVENTIONS.md](CONVENTIONS.md)

## Модули

По одному файлу на каталог `src/<mod>/`:

- [modules/common.md](modules/common.md)
- [modules/config.md](modules/config.md)
- [modules/core.md](modules/core.md)
- [modules/fs.md](modules/fs.md)
- [modules/gsm.md](modules/gsm.md)
- [modules/io.md](modules/io.md)
- [modules/json.md](modules/json.md) — помощники JSON лежат в `src/json/`
- [modules/modem.md](modules/modem.md)
- [modules/mqtt.md](modules/mqtt.md)
- [modules/ota.md](modules/ota.md)
- [modules/program.md](modules/program.md)
- [modules/web.md](modules/web.md)

Полевой лист: [modules/gsm_mqtt_field_log.md](modules/gsm_mqtt_field_log.md).
