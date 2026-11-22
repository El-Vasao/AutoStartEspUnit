# Заметки по порту ESP32-C3

Ветка: `esp32`. Окружение PlatformIO: **`esp32c3`** (`python build.py release|debug|fs`).

## Пины (ESP-C3-12F / DevKit на сетях платы ESP-12)

| Сеть | GPIO ESP32-C3 | Примечание |
|------|---------------|------------|
| GSM TX (MCU→SIM800) | 21 | UART0 TX |
| GSM RX (SIM800→MCU) | 20 | UART0 RX |
| 1-Wire | 9 | strap: держать HIGH при загрузке |
| Кнопка / IN3 | 10 | active_state LOW (на GND); в UI не оставлять HIGH |
| IN1 / IN2 | 5 / 4 | |
| RELAY1..5 | 3, 19, 18, 8, 2 | GPIO8 — strap, лучше HIGH при загрузке |
| VBAT ADC | 1 | 1:15 → ~0–1 В; `ADC_0db` |

См. `include/common/Pins.h`.

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
