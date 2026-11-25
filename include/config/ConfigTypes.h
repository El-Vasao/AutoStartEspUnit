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
struct GsmConfig {
    char apn[TextBytes::Gsm::APN];
    char apn_user[TextBytes::Gsm::APN_USER];
    char apn_pass[TextBytes::Gsm::APN_PASS];

    char owner_phone[TextBytes::Gsm::PHONE];
    char dtmf_password[TextBytes::Gsm::DTMF_PASSWORD];

    GsmConfig() {
        strcpy(apn, "internet");
        apn_user[0] = '\0';
        apn_pass[0] = '\0';
        owner_phone[0] = '\0';
        dtmf_password[0] = '\0';
    }
};

// ------------------------------------------------------------------
// MQTT
// ------------------------------------------------------------------
struct MqttConfig {
    char broker[TextBytes::Mqtt::BROKER];
    uint16_t port;
    char client_id[TextBytes::Mqtt::CLIENT_ID];
    char user[TextBytes::Mqtt::USER];
    char pass[TextBytes::Mqtt::PASS];
    /// Базовый путь для фиксированных суффиксов `/online`, `/state`, `/cmd`, `/reply` и каталогов.
    char topic_prefix[TextBytes::Mqtt::TOPIC_PREFIX];
    uint16_t publish_interval_sec;

    MqttConfig() : port(1883), publish_interval_sec(300) {
        broker[0] = '\0';
        client_id[0] = '\0';
        user[0] = '\0';
        pass[0] = '\0';
        strcpy(topic_prefix, "car");
    }
};

// ------------------------------------------------------------------
// Автомобиль (ADC, стартер и т.д.)
// ------------------------------------------------------------------
/// Как шаг ENGINE_START определяет успешный запуск (JSON: "timed" / "rpm" / "input").
enum class StartMethod : uint8_t { Timed = 0, Rpm, Input };

struct VehicleConfig {
    float adc_voltage_coeff;
    uint16_t starter_max_time_sec;
    uint16_t wait_after_start_sec;
    float engine_running_voltage_threshold;
    float engine_stopped_voltage_threshold;
    uint8_t starter_relay_id; ///< номер реле 1…n (K1…); 0 недопустим для стартера

    StartMethod start_method;
    uint8_t start_input_id;           ///< номер входа 1…n или 0 = нет
    float starter_crank_time_sec;     ///< длительность прокрутки для timed
    uint16_t starter_retry_pause_sec;
    uint8_t starter_attempts;
    uint8_t start_block_input_id;     ///< номер входа 1…n или 0 = нет

    char engine_detection_source[TextBytes::Vehicle::ENGINE_DETECTION_SOURCE];
    uint8_t engine_input_id;          ///< номер входа 1…n (IN1…)

    VehicleConfig()
        : adc_voltage_coeff(Adc::DEFAULT_COEFF),
          starter_max_time_sec(StarterLimits::MAX_TIME_DEFAULT_SEC),
          wait_after_start_sec(StarterLimits::WAIT_AFTER_START_DEFAULT_SEC),
          engine_running_voltage_threshold(StarterLimits::RUNNING_VOLTAGE_DEFAULT),
          engine_stopped_voltage_threshold(StarterLimits::STOPPED_VOLTAGE_DEFAULT),
          starter_relay_id(3), ///< K3
          start_method(StartMethod::Timed),
          start_input_id(0),
          starter_crank_time_sec(StarterLimits::CRANK_TIME_DEFAULT_SEC),
          starter_retry_pause_sec(StarterLimits::RETRY_PAUSE_DEFAULT_SEC),
          starter_attempts(StarterLimits::ATTEMPTS_DEFAULT),
          start_block_input_id(2), ///< IN2 нейтраль
          engine_input_id(2) { ///< IN2
        strcpy(engine_detection_source, "voltage");
    }
};

// ------------------------------------------------------------------
// Конфигурация датчика температуры
// ------------------------------------------------------------------
struct SensorConfig {
    uint8_t id; ///< номер 1…n (T1…); 0 = слот ещё не заполнен
    float coeff;
    char name[TextBytes::Sensors::NAME];
    char rom[TextBytes::Sensors::ROM];

    SensorConfig() : id(0), coeff(0.0f) {
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
    uint8_t id; ///< номер 1…n (IN1…)
    bool enabled;
    InputType type;
    bool active_state;
    char name[TextBytes::Inputs::NAME];
    PulseCounterConfig pulse;

    InputConfig() : id(0), enabled(true), type(InputType::DIGITAL), active_state(true) {
        name[0] = '\0';
    }
};

// ------------------------------------------------------------------
// Реле (публичный id K1…, имя редактируется)
// ------------------------------------------------------------------
struct RelayConfig {
    uint8_t id; ///< номер 1…n (K1…)
    char name[TextBytes::Relays::NAME];

    RelayConfig() : id(0) { name[0] = '\0'; }
};

// ------------------------------------------------------------------
// Триггеры на входах (статические массивы)
// ------------------------------------------------------------------
struct TriggerConfig {
    uint16_t id;
    bool enabled;
    uint8_t input_id; ///< номер входа 1…n или 0
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
    uint8_t sensor_id; ///< номер датчика 1…n (T1…) или 0
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
    uint8_t sensor_id; ///< номер датчика 1…n (T1…) или 0
    char comparison[TextBytes::Triggers::COMPARISON];
    float lower_threshold;
    float upper_threshold;
    uint8_t program_id_lower;
    uint8_t program_id_upper;

    ThermostatConfig()
        : enabled(false),
          sensor_id(1), ///< T1
          lower_threshold(-15.0f),
          upper_threshold(10.0f),
          program_id_lower(1),
          program_id_upper(2) {
        strcpy(comparison, "above");
    }
};

// ------------------------------------------------------------------
// Защита АКБ
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
          program_id(1) {}
};

// ------------------------------------------------------------------
// Настенные часы / NTP (SIM800 CNTP)
// ------------------------------------------------------------------
struct TimeConfig {
    bool enabled;
    char ntp_server[TextBytes::TimeCfg::NTP_SERVER];
    int8_t tz_offset_hours; ///< Смещение от UTC в целых часах (МСК = +3)
    uint32_t sync_interval_hours; ///< Период между успешными синхронизациями (целые часы)

    /// Значения по умолчанию в RAM и при разборе: синхронизация выключена, пока конфиг (или заводской JSON DefaultConfig) её не включит.
    /// Чтобы старый /config.json без секции `time` внезапно не запустил NTP.
    TimeConfig()
        : enabled(false),
          tz_offset_hours(3),
          sync_interval_hours(6) {
        ntp_server[0] = '\0';
    }
};

// ------------------------------------------------------------------
// Шаг программы
// ------------------------------------------------------------------
struct Step {
    uint8_t step;
    char action[TextBytes::Programs::STEP_ACTION];
    uint8_t relay_id; ///< номер реле 1…n (K1…) или 0
    uint32_t ms;
    uint8_t input_id; ///< номер входа 1…n (IN1…) или 0
    uint16_t input_trigger_id;
    uint16_t temp_trigger_id;
    char message[TextBytes::Programs::STEP_MESSAGE];

    Step()
        : step(0),
          relay_id(0),
          ms(0),
          input_id(0),
          input_trigger_id(0),
          temp_trigger_id(0) {
        action[0] = '\0';
        message[0] = '\0';
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
    /// Если true — стартовать в SETUP_AP после заводских значений; с FE обычно не шлётся → false в JSON.
    bool setup_required{false};
    WifiConfig wifi;
    GsmConfig gsm;
    MqttConfig mqtt;
    VehicleConfig vehicle;
    ThermostatConfig thermostat;
    BatterySaverConfig battery_saver;
    TimeConfig time;

    SensorConfig sensors[HardwareLimits::SENSORS];
    InputConfig inputs[HardwareLimits::INPUTS];
    RelayConfig relays[HardwareLimits::RELAYS];

    TriggerConfig input_triggers[Limits::MAX_TRIGGERS];
    uint8_t input_triggers_count{0};
    TempTriggerConfig temperature_triggers[Limits::MAX_TRIGGERS];
    uint8_t temperature_triggers_count{0};

    BaseConfig() = default;
};

