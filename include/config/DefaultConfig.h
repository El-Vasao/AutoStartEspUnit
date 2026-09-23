// include/config/DefaultConfig.h
#pragma once

#include <Arduino.h>

// Дефолтный конфиг в формате JSON (сохранён в PROGMEM).
// Примечание: должен соответствовать `Config.h` и `settings.schema.json`, иначе устройство уйдёт в EMERGENCY_AP.
static const char DEFAULT_CONFIG_JSON[] PROGMEM = R"({
  "setup_required": true,
  "wifi": {
    "ssid": "AutoStart-Setup",
    "password": "12345678",
    "ap_timeout_sec": 0,
    "ap_timeout_enabled": false
  },
  "gsm": {
    "apn": "internet.yota",
    "apn_user": "",
    "apn_pass": "",
    "owner_phone": "",
    "dtmf_password": ""
  },
  "mqtt": {
    "broker": "m5.wqtt.ru",
    "port": 10162,
    "client_id": "",
    "user": "u_1WJDYV",
    "pass": "9chZ2EvV",
    "topic_prefix": "car/Subaru",
    "publish_interval_sec": 60
  },
  "vehicle": {
    "adc_voltage_coeff": 10,
    "starter_max_time_sec": 5,
    "wait_after_start_sec": 10,
    "engine_running_voltage_threshold": 13.2,
    "engine_stopped_voltage_threshold": 12.8,
    "starter_relay_id": 2002,
    "engine_detection_source": "voltage",
    "engine_input_id": 1001
  },
  "sensors": [
    { "id": 3001, "rom": "", "coeff": 0.1, "name": "" },
    { "id": 3002, "rom": "", "coeff": 0.1, "name": "" },
    { "id": 3003, "rom": "", "coeff": 0.1, "name": "" }
  ],
  "inputs": [
    {
      "id": 1001,
      "enabled": true,
      "type": 0,
      "active_state": true,
      "name": "",
      "pulse": { "pulses_per_rev": 1, "threshold_rpm": 0 }
    },
    {
      "id": 1002,
      "enabled": true,
      "type": 0,
      "active_state": true,
      "name": "",
      "pulse": { "pulses_per_rev": 1, "threshold_rpm": 0 }
    },
    {
      "id": 1003,
      "enabled": true,
      "type": 0,
      "active_state": true,
      "name": "",
      "pulse": { "pulses_per_rev": 1, "threshold_rpm": 0 }
    }
  ],
  "thermostat": {
    "enabled": false,
    "sensor_id": 0,
    "comparison": "above",
    "lower_threshold": 5.0,
    "upper_threshold": 10.0,
    "program_id_lower": 0,
    "program_id_upper": 0
  },
  "battery_saver": {
    "enabled": false,
    "voltage_start_threshold": 11.8,
    "hysteresis": 0.2,
    "min_low_voltage_duration_sec": 30,
    "min_time_between_attempts_sec": 60,
    "max_attempts_per_day": 3,
    "program_id": 0
  },
  "time": {
    "enabled": true,
    "ntp_server": "ru.pool.ntp.org",
    "tz_offset_hours": 3,
    "sync_interval_sec": 21600
  },
  "input_triggers": [],
  "input_triggers_count": 0,
  "temperature_triggers": [],
  "temperature_triggers_count": 0,
  "schedule_triggers": [],
  "schedule_triggers_count": 0
})";

