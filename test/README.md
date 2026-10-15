# Host tests (`pio test -e native`)

Unity suites for pure parsers/contracts (no GSM/Web/FS hardware):

| Suite | Coverage |
|-------|----------|
| `test_gsm_at_parse` | AT line parsers (`GsmAtParse`) |
| `test_mqtt_command_parser` | MQTT cmd JSON (`MqttCommandParser`) |
| `test_program_action` | `ActionId` string map |
| `test_mqtt_protocol` | MQTT codes / RX stall / ack-gate contracts (mirrored helpers) |
| `test_rpm_estimator` | RPM from edge periods, noise filter, zero timeout, 100 ms confirm (`RpmEstimator`) |
| `test_vehicle_rules` | Vehicle/starter settings: load-time normalize, strict save validation (`vehicle_rules`) |
| `test_starter_machine` | ENGINE_START state machine: timed / rpm / input, retries, interlock (`starter::Machine`) |

```bat
pio test -e native
```

Requires host `g++` (MinGW/WinLibs). On Windows, if the username contains a space, `test/native_pre.py` sets `GCC_EXEC_PREFIX` to the 8.3 short path so `ld` can find `default-manifest.o`.

Field checklist [`docs/modules/gsm_mqtt_field_log.md`](../docs/modules/gsm_mqtt_field_log.md) remains the primary regression gate on hardware.
