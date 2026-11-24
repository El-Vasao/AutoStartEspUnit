# Железо AutoStart (ESP32-C3)

Ветка: `esp32`. Окружение PlatformIO: **`esp32c3`** (`python build.py release|fs`).

## Модуль MCU

Поддерживаемый модуль: **XH-C3F** (форм-фактор посадочного места ESP-12E/F).

**Не** Ai-Thinker ESP-C3-12F: у него другая разводка тех же площадок. Карта пинов прошивки — только под XH-C3F.

Полная карта: [`include/common/Pins.h`](../include/common/Pins.h).

## UART

| Роль | Пины | Примечание |
|------|------|------------|
| SIM800 (AT/MQTT) | UART0: TX IO21, RX IO20 | Только модем |
| Диагностический журнал | UART1 TX: **IO7** (`Pin::LOG_TX`), RX dummy IO6 | 115200 8N1 |

### Журнал на IO7

Espressif для ESP32-C3 (ESP-AT) сажает UART1 на **GPIO6 (RX) / GPIO7 (TX)**. Прошивка делает так же: TX=IO7, RX=IO6 (пин не выводим на разъём).

**IO2 не используем для журнала:** это strap-пин; с подключённым USB-UART на нём часто полная тишина.

Подключение: **чип IO7 (MCU TX) → RX адаптера**, общий GND, **115200 8N1**.

Порт в `initVariant()` до `setup()`. Порт прошивки (UART0) журнал не показывает.

### Если COM адаптера молчит

1. **Поменять RX↔TX** (MCU TX → адаптер RX). Адаптер «исправен» ≠ правильная пара к ESP.
2. COM — порт адаптера на IO7, не UART0 прошивки.
3. 115200 8N1, общий GND, логика 3.3 В.
4. SoftAP UI показывает логи → прошивка жива, смотреть только проводку IO7.
5. Loopback адаптера (TX–RX на адаптере без ESP): эхо в терминале = адаптер/COM ок.

## Разделы flash

[`partitions.csv`](../partitions.csv) — два слота OTA (`app0`/`app1`) и ФС с меткой `spiffs`. ФС ~1,4 МБ.

## UI SoftAP — 3 gzip-ассета

После `python build.py fs`: `index.html.gz`, `style.css.gz`, `app.js.gz`.

## Сборка

```bash
python build.py release -u
python build.py fs
```

`include/common/Version.h` генерируется, в git не входит.

## RAM

~170 КБ свободной кучи вскоре после загрузки на C3.
