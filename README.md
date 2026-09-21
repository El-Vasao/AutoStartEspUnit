# AutoStartEspUnit

Car remote auto-start firmware (SoftAP UI + SIM800 MQTT).

**Target:** ESP32-C3 (ESP-C3-12F / DevKit; same PCB nets as ESP-12E).

## Branches

- `esp32` — active (ESP32-C3)
- `main` — stable after acceptance
- `esp8266-dead-end` / tag `esp8266-final` — frozen archive

## Build

```bash
python build.py release -u
python build.py fs
```

See [`docs/ESP32C3.md`](docs/ESP32C3.md) and [`docs/build_and_flash.md`](docs/build_and_flash.md).

Remote: https://github.com/El-Vasao/AutoStartEspUnit
