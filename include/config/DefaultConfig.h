// include/config/DefaultConfig.h
#pragma once

#include <Arduino.h>

// Дефолтный конфиг в формате JSON (сохранён в PROGMEM).
// Должен проходить `data/validation/settings.schema.json` (проверяет test/js/contract.test.mjs),
// иначе устройство уйдёт в EMERGENCY_AP.
// Публичные id каналов — шёлк K/IN/T с единицы; подписи входов и реле заданы.
// Обезличенный «из коробки» профиль: без телефона/MQTT/ROM стенда; триггеры и термо/АКБ
// предпривязаны к заводским программам, но выключены до настройки установщиком.
//
// Входы: IN1 Brake pedal SW (триггер → «Стоп»), IN2 Gear neutral SW (блокировка ENGINE_START;
// логически активен = запрет; active_state под полярность датчика — по умолчанию true),
// IN3 Button (active low).
static const char DEFAULT_CONFIG_JSON[] PROGMEM = R"({
  "setup_required": true,
  "wifi": {
    "ssid": "AutoStart-Setup",
    "password": "12345678",
    "ap_timeout_sec": 0,
    "ap_timeout_enabled": false
  },
  "gsm": {
    "apn": "internet",
    "apn_user": "",
    "apn_pass": "",
    "owner_phone": "",
    "dtmf_password": ""
  },
  "mqtt": {
    "broker": "",
    "port": 1883,
    "client_id": "",
    "user": "",
    "pass": "",
    "topic_prefix": "car",
    "publish_interval_sec": 300
  },
  "vehicle": {
    "adc_voltage_coeff": 0.004,
    "starter_max_time_sec": 5,
    "wait_after_start_sec": 10,
    "engine_running_voltage_threshold": 13.2,
    "engine_stopped_voltage_threshold": 12.8,
    "starter_relay_id": "K3",
    "start_method": "timed",
    "start_input_id": "",
    "starter_crank_time_sec": 2,
    "starter_retry_pause_sec": 5,
    "starter_attempts": 3,
    "start_block_input_id": "IN2",
    "engine_detection_source": "voltage",
    "engine_input_id": "IN2"
  },
  "sensors": [
    { "id": "T1", "rom": "", "coeff": 0, "name": "Cabin" },
    { "id": "T2", "rom": "", "coeff": 0, "name": "Engine" },
    { "id": "T3", "rom": "", "coeff": 0, "name": "Outside" }
  ],
  "inputs": [
    {
      "id": "IN1",
      "enabled": true,
      "type": 0,
      "active_state": true,
      "name": "Brake pedal SW",
      "pulse": { "pulses_per_rev": 1, "threshold_rpm": 0 }
    },
    {
      "id": "IN2",
      "enabled": true,
      "type": 0,
      "active_state": true,
      "name": "Gear neutral SW",
      "pulse": { "pulses_per_rev": 1, "threshold_rpm": 0 }
    },
    {
      "id": "IN3",
      "enabled": true,
      "type": 0,
      "active_state": false,
      "name": "Button",
      "pulse": { "pulses_per_rev": 1, "threshold_rpm": 0 }
    }
  ],
  "relays": [
    { "id": "K1", "name": "ACC" },
    { "id": "K2", "name": "IGN" },
    { "id": "K3", "name": "START" },
    { "id": "K4", "name": "LOCK" },
    { "id": "K5", "name": "BYPASS" }
  ],
  "thermostat": {
    "enabled": false,
    "sensor_id": "T1",
    "comparison": "above",
    "lower_threshold": -15.0,
    "upper_threshold": 10.0,
    "program_id_lower": 1,
    "program_id_upper": 2
  },
  "battery_saver": {
    "enabled": false,
    "voltage_start_threshold": 11.8,
    "voltage_abort_threshold": 10.5,
    "hysteresis": 0.5,
    "min_low_voltage_duration_sec": 180,
    "min_time_between_attempts_sec": 1800,
    "max_attempts_per_day": 3,
    "program_id": 1
  },
  "time": {
    "enabled": true,
    "ntp_server": "ru.pool.ntp.org",
    "tz_offset_hours": 3,
    "sync_interval_hours": 6
  },
  "input_triggers": [
    { "id": 1, "enabled": false, "input_id": "IN1", "trigger_level": 1, "program_id": 2 },
    { "id": 2, "enabled": false, "input_id": "IN2", "trigger_level": 1, "program_id": 0 },
    { "id": 3, "enabled": false, "input_id": "IN3", "trigger_level": 1, "program_id": 1 }
  ],
  "temperature_triggers": [
    { "id": 1, "enabled": false, "sensor_id": "T1", "comparison": "below", "threshold": -10, "program_id": 1 },
    { "id": 2, "enabled": false, "sensor_id": "T2", "comparison": "above", "threshold": 105, "program_id": 2 },
    { "id": 3, "enabled": false, "sensor_id": "T3", "comparison": "below", "threshold": -15, "program_id": 1 }
  ]
})";
