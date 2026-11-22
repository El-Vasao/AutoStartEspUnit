# Заметки по порту ESP32-C3

Ветка: `esp32`. Окружение PlatformIO: **`esp32c3`** (`python build.py release|debug|fs`).

## Пины

Полная карта — только в [`include/common/Pins.h`](../include/common/Pins.h) (XH-C3F на сетях ESP-12; не Ai-Thinker C3-12F).

## Разделы

[`partitions.csv`](../partitions.csv) — два слота OTA (`app0`/`app1`) и ФС с меткой `spiffs` (её монтирует Arduino LittleFS). ФС около 1,4 МБ.

## UART0 (SIM800 и отладка)

SIM800 и отладочный Serial делят UART0 (GPIO20/21).

## UI SoftAP — 3 gzip-ассета

После `python build.py fs`:

1. `index.html.gz`
2. `style.css.gz`
3. `app.js.gz`

## Сборка

```bash
python build.py release -u   # прошивка
python build.py fs           # UI в LittleFS
python build.py debug -u     # SERIAL_DEBUG на общем UART
```

`include/common/Version.h` генерируется и не попадает в git.

## RAM

На поле: около 170 КБ свободной кучи вскоре после загрузки на C3 — запас SoftAP/UI больше не упирается в узкое место ESP8266.
