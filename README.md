# AutoStartEspUnit

Car remote auto-start firmware (SoftAP UI + SIM800 MQTT).

**Target:** ESP32-C3 (ESP-C3-12F / DevKit; same PCB nets as ESP-12E). SoftAP is a trusted network: no API/OTA auth.

## Build

```bash
python build.py release -u
python build.py fs
pio test -e native
node --test "test/js/**/*.test.mjs"
```

See [`AGENTS.md`](AGENTS.md), [`docs/HARDWARE.md`](docs/HARDWARE.md), [`docs/BUILD.md`](docs/BUILD.md), [`docs/TESTING.md`](docs/TESTING.md).

Remote: https://github.com/El-Vasao/AutoStartEspUnit
