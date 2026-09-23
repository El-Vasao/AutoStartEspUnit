// include/config/ConfigTypes.h
#pragma once

#include <Arduino.h>
#include <cstring>

#include "common/Constants.h"

// ====================================================
// Структуры конфигурации
// ====================================================
//
// Контракт:
// - базовый конфиг хранится в LittleFS как JSON (/config.json) и кэшируется в RAM (BaseConfig);
// - строки в конфиге — фиксированные char[] (не String), чтобы избежать фрагментации heap;
// - глубокая валидация схемы отключена для экономии RAM/flash; маппинг только по известным полям.
//
// Если добавляете поля в структуры ниже — ОБЯЗАТЕЛЬНО обновите:
// - базовый JSON: src/config/Config.BaseSax.cpp (SAX-load), Config.BaseJsonEmit.cpp (emit + CRC),
// - программы/индекс: src/config/Config.ProgramJsonIo.cpp,
// - DefaultConfig (дефолтный JSON в PROGMEM),
// - веб-UI (если поле отображается/редактируется).

// ------------------------------------------------------------------
// Wi-Fi
// ------------------------------------------------------------------
struct WifiConfig {
    char ssid[TextBytes::Wifi::SSID];
    char password[TextBytes::Wifi::PASSWORD];
    uint16_t ap_timeout_sec;
    bool ap_timeout_enabled;

    WifiConfig() : ap_timeout_sec(0), ap_timeout_enabled(false) {
        strcpy(ssid, "AutoStart-Setup");
        strcpy(password, "12345678");
    }
};

// ------------------------------------------------------------------
// GSM
// ------------------------------------------------------------------
struct GSMConfig {
    char apn[TextBytes::Gsm::APN];
    char apn_user[TextBytes::Gsm::APN_USER];
    char apn_pass[TextBytes::Gsm::APN_PASS];

    char owner_phone[TextBytes::Gsm::PHONE];
    char dtmf_password[TextBytes::Gsm::DTMF_PASSWORD];

    GSMConfig() {
        strcpy(apn, "internet.mts.ru");
        apn_user[0] = '\0';
        apn_pass[0] = '\0';
        owner_phone[0] = '\0';
        dtmf_password[0] = '\0';
    }
};

// ------------------------------------------------------------------
// MQTT
// ------------------------------------------------------------------
struct MQTTConfig {
    char broker[TextBytes::Mqtt::BROKER];
    uint16_t port;
    char client_id[TextBytes::Mqtt::CLIENT_ID];
    char user[TextBytes::Mqtt::USER];
    char pass[TextBytes::Mqtt::PASS];
    /// Base path for fixed suffixes `/avail`, `/status`, `/cmd`, `/reply`.
    char topic_prefix[TextBytes::Mqtt::TOPIC_PREFIX];
    uint16_t publish_interval_sec;

    MQTTConfig() : port(1883), publish_interval_sec(30) {
        strcpy(broker, "mqtt.example.com");
        strcpy(client_id, "autostart-ABC123");
        user[0] = '\0';
        pass[0] = '\0';
        strcpy(topic_prefix, "car/ABC123");
    }
};

// ------------------------------------------------------------------
// Автомобиль (ADC, стартер и т.д.)
// ------------------------------------------------------------------
struct VehicleConfig {
    float adc_voltage_coeff;
    uint16_t starter_max_time_sec;
    uint16_t wait_after_start_sec;
    float engine_running_voltage_threshold;
    float engine_stopped_voltage_threshold;
    uint16_t starter_relay_id;

    char engine_detection_source[TextBytes::Vehicle::ENGINE_DETECTION_SOURCE];
    uint16_t engine_input_id;

    VehicleConfig()
        : adc_voltage_coeff(ADC::DEFAULT_COEFF),
          starter_max_time_sec(5),
          wait_after_start_sec(10),
          engine_running_voltage_threshold(13.2f),
          engine_stopped_voltage_threshold(12.8f),
          starter_relay_id(2002),
          engine_input_id(1001) {
        strcpy(engine_detection_source, "voltage");
    }
};

// ------------------------------------------------------------------
// Конфигурация датчика температуры
// ------------------------------------------------------------------
struct SensorConfig {
    uint16_t id;
    float coeff;
    char name[TextBytes::Sensors::NAME];
    char rom[TextBytes::Sensors::ROM];

    SensorConfig() : id(0), coeff(0.1f) {
        name[0] = '\0';
        rom[0] = '\0';
    }
};

// ====================================================
// Конфигурация цифрового входа
// ====================================================
enum class InputType : uint8_t { DIGITAL = 0, PULSE_COUNTER };

struct PulseCounterConfig {
    uint16_t pulses_per_rev;
    uint16_t threshold_rpm;

    PulseCounterConfig() : pulses_per_rev(1), threshold_rpm(0) {}
};

struct InputConfig {
    uint16_t id;
    bool enabled;
    InputType type;
    bool active_state;
    char name[TextBytes::Inputs::NAME];
    PulseCounterConfig pulse;

    InputConfig() : id(0), enabled(true), type(InputType::DIGITAL), active_state(LOW) {
        name[0] = '\0';
    }
};

// ------------------------------------------------------------------
// Триггеры на входах (статические массивы)
// ------------------------------------------------------------------
struct TriggerConfig {
    uint16_t id;
    bool enabled;
    uint16_t input_id;
    uint8_t trigger_level;
    uint8_t program_id;

    TriggerConfig() : id(0), enabled(false), input_id(0), trigger_level(1), program_id(0) {}
};

// ------------------------------------------------------------------
// Температурные триггеры (статические массивы)
// ------------------------------------------------------------------
struct TempTriggerConfig {
    bool enabled;
    uint16_t id;
    uint16_t sensor_id;
    char comparison[TextBytes::Triggers::COMPARISON];
    float threshold;
    uint8_t program_id;

    TempTriggerConfig() : enabled(false), id(0), sensor_id(0), threshold(0.0f), program_id(0) {
        strcpy(comparison, "above");
    }
};

// ------------------------------------------------------------------
// Термостат
// ------------------------------------------------------------------
struct ThermostatConfig {
    bool enabled;
    uint16_t sensor_id;
    char comparison[TextBytes::Triggers::COMPARISON];
    float lower_threshold;
    float upper_threshold;
    uint8_t program_id_lower;
    uint8_t program_id_upper;

    ThermostatConfig()
        : enabled(false),
          sensor_id(0),
          lower_threshold(5.0f),
          upper_threshold(10.0f),
          program_id_lower(0),
          program_id_upper(0) {
        strcpy(comparison, "above");
    }
};

// ------------------------------------------------------------------
// Battery Saver
// ------------------------------------------------------------------
struct BatterySaverConfig {
    bool enabled;
    float voltage_start_threshold;
    float voltage_abort_threshold;
    float hysteresis;
    uint16_t min_low_voltage_duration_sec;
    uint16_t min_time_between_attempts_sec;
    uint8_t max_attempts_per_day;
    uint8_t program_id;

    BatterySaverConfig()
        : enabled(false),
          voltage_start_threshold(11.8f),
          voltage_abort_threshold(10.5f),
          hysteresis(0.5f),
          min_low_voltage_duration_sec(180),
          min_time_between_attempts_sec(1800),
          max_attempts_per_day(3),
          program_id(0) {}
};

// ------------------------------------------------------------------
// Wall clock / NTP (SIM800 CNTP)
// ------------------------------------------------------------------
struct TimeConfig {
    bool enabled;
    char ntp_server[TextBytes::TimeCfg::NTP_SERVER];
    int8_t tz_offset_hours; ///< UTC offset in whole hours (MSK = +3)
    uint32_t sync_interval_sec;

    /// RAM/parse defaults: sync off until config (or DefaultConfig factory JSON) enables it.
    /// Avoids surprise NTP on old /config.json without a `time` section.
    TimeConfig()
        : enabled(false),
          tz_offset_hours(3),
          sync_interval_sec(21600) {
        ntp_server[0] = '\0';
    }
};

// ------------------------------------------------------------------
// Schedule triggers (local HH:MM → program)
// ------------------------------------------------------------------
struct ScheduleTriggerConfig {
    uint16_t id;
    bool enabled;
    uint8_t hour;
    uint8_t minute;
    uint8_t days_mask; // bit0=Mon … bit6=Sun; 0x7F = every day
    uint8_t program_id;

    ScheduleTriggerConfig()
        : id(0),
          enabled(false),
          hour(0),
          minute(0),
          days_mask(0x7F),
          program_id(0) {}
};

// ------------------------------------------------------------------
// Шаг программы
// ------------------------------------------------------------------
struct Step {
    uint8_t step;
    char action[TextBytes::Programs::STEP_ACTION];
    uint16_t relay_id;
    uint32_t ms;
    uint32_t timeout_ms;

    uint16_t input_id;
    uint16_t sensor_id;
    char sensor_name[TextBytes::Sensors::NAME];
    uint16_t input_trigger_id;
    uint16_t temp_trigger_id;
    uint8_t program_id;
    uint8_t retries;
    uint8_t expected_state;
    char comparison[TextBytes::Triggers::COMPARISON];
    float threshold;
    uint8_t engine_state;
    uint8_t timeout_action;
    uint8_t skip_count;

    Step()
        : step(0),
          relay_id(0),
          ms(0),
          timeout_ms(0),
          input_id(0),
          sensor_id(0),
          input_trigger_id(0),
          temp_trigger_id(0),
          program_id(0),
          retries(1),
          expected_state(1),
          threshold(0.0f),
          engine_state(1),
          timeout_action(0),
          skip_count(0) {
        action[0] = '\0';
        sensor_name[0] = '\0';
        strcpy(comparison, "above");
    }
};

// ------------------------------------------------------------------
// Программа
// ------------------------------------------------------------------
struct Program {
    uint8_t id;
    char name[TextBytes::Programs::NAME];
    Step steps[Limits::MAX_STEPS_PER_PROGRAM];
    uint8_t step_count;

    Program() : id(0), step_count(0) { name[0] = '\0'; }
};

// ====================================================
// BaseConfig – часто используемые данные (всегда в RAM)
// ====================================================
struct BaseConfig {
    /// Если true — стартовать в SETUP_AP после factory default; с FE обычно не шлётся → false в JSON.
    bool setup_required;
    WifiConfig wifi;
    GSMConfig gsm;
    MQTTConfig mqtt;
    VehicleConfig vehicle;
    ThermostatConfig thermostat;
    BatterySaverConfig battery_saver;
    TimeConfig time;

    SensorConfig sensors[HardwareLimits::SENSORS];
    InputConfig inputs[HardwareLimits::INPUTS];

    TriggerConfig input_triggers[Limits::MAX_TRIGGERS];
    uint8_t input_triggers_count;
    TempTriggerConfig temperature_triggers[Limits::MAX_TRIGGERS];
    uint8_t temperature_triggers_count;
    ScheduleTriggerConfig schedule_triggers[Limits::MAX_SCHEDULE_TRIGGERS];
    uint8_t schedule_triggers_count;

    BaseConfig() = default;
};

