# ESP32-C3 port notes

Branch: `esp32`. PlatformIO env: **`esp32c3`** (`python build.py release|debug|fs`).

## Pins (ESP-C3-12F / DevKit on ESP-12 PCB nets)

| Net | ESP32-C3 GPIO | Notes |
|-----|---------------|--------|
| GSM TX (MCU→SIM800) | 21 | UART0 TX |
| GSM RX (SIM800→MCU) | 20 | UART0 RX |
| 1-Wire | 9 | strap: keep HIGH at boot |
| Button / IN3 | 10 | |
| IN1 / IN2 | 5 / 4 | |
| RELAY1..5 | 3, 19, 18, 8, 2 | GPIO8 strap — prefer HIGH at boot |
| VBAT ADC | 1 | 1:15 → ~0–1 V; `ADC_0db` |

See `include/common/Pins.h`.

## Partitions

[`partitions.csv`](../partitions.csv) — dual OTA (`app0`/`app1`) + FS labeled `spiffs` subtype (Arduino LittleFS mounts it). ~1.4 MB FS.

## UART legacy

SIM800 and debug Serial share UART0 (GPIO20/21).

## SoftAP UI — 3 gzip assets

After `python build.py fs`:

1. `index.html.gz`
2. `style.css.gz`
3. `app.js.gz`

## Build

```bash
python build.py release -u   # firmware
python build.py fs           # LittleFS UI
python build.py debug -u     # SERIAL_DEBUG on shared UART
```

`include/common/Version.h` is generated and gitignored.

## RAM

Field note: ~170 KB free heap shortly after boot on C3 — SoftAP/UI headroom is no longer the ESP8266 bottleneck.
