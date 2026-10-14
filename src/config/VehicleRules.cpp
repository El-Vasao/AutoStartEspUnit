// src/config/VehicleRules.cpp
#include "config/VehicleRules.h"

#include <math.h>
#include <string.h>

namespace vehicle_rules {

namespace {

constexpr float VOLTAGE_MIN = 5.0f;
constexpr float VOLTAGE_MAX = 30.0f;

bool voltageInRange(float v) { return isfinite(v) && v >= VOLTAGE_MIN && v <= VOLTAGE_MAX; }

bool crankInRange(float v) {
    return isfinite(v) && v >= StarterLimits::CRANK_TIME_MIN_SEC && v <= StarterLimits::CRANK_TIME_MAX_SEC;
}

bool maxTimeInRange(uint16_t v) {
    return v >= StarterLimits::MAX_TIME_MIN_SEC && v <= StarterLimits::MAX_TIME_MAX_SEC;
}

bool waitInRange(uint16_t v) {
    return v >= StarterLimits::WAIT_AFTER_START_MIN_SEC && v <= StarterLimits::WAIT_AFTER_START_MAX_SEC;
}

bool attemptsInRange(uint8_t v) {
    return v >= StarterLimits::ATTEMPTS_MIN && v <= StarterLimits::ATTEMPTS_MAX;
}

bool usesStartInput(StartMethod m) { return m == StartMethod::Rpm || m == StartMethod::Input; }

void report(NoteFn note, void* ctx, const char* msg, uint8_t& fixes) {
    fixes++;
    if (note) note(msg, ctx);
}

} // namespace

const char* startMethodToString(StartMethod m) {
    switch (m) {
        case StartMethod::Rpm:   return "rpm";
        case StartMethod::Input: return "input";
        case StartMethod::Timed:
        default:                 return "timed";
    }
}

StartMethod startMethodFromString(const char* s, StartMethod def) {
    if (!s) return def;
    if (strcmp(s, "timed") == 0) return StartMethod::Timed;
    if (strcmp(s, "rpm") == 0) return StartMethod::Rpm;
    if (strcmp(s, "input") == 0) return StartMethod::Input;
    return def;
}

int8_t findInput(const BaseConfig& cfg, uint16_t inputId) {
    if (inputId == 0) return -1;
    for (uint8_t i = 0; i < HardwareLimits::INPUTS; i++) {
        if (cfg.inputs[i].id == inputId) return static_cast<int8_t>(i);
    }
    return -1;
}

const char* checkStartInput(const BaseConfig& cfg, StartMethod method, uint16_t inputId) {
    if (!usesStartInput(method)) return nullptr;
    int8_t idx = findInput(cfg, inputId);
    if (idx < 0) return "start input not found";
    const InputConfig& in = cfg.inputs[idx];
    if (!in.enabled) return "start input disabled";
    if (method == StartMethod::Rpm) {
        if (in.type != InputType::PULSE_COUNTER) return "rpm start needs a pulse input";
        if (in.pulse.threshold_rpm == 0) return "rpm start needs threshold_rpm > 0";
    } else {
        if (in.type != InputType::DIGITAL) return "input start needs a digital input";
    }
    return nullptr;
}

const char* checkBlockInput(const BaseConfig& cfg) {
    const VehicleConfig& v = cfg.vehicle;
    if (v.start_block_input_id == 0) return nullptr;
    int8_t idx = findInput(cfg, v.start_block_input_id);
    if (idx < 0) return "start block input not found";
    if (cfg.inputs[idx].type != InputType::DIGITAL) return "start block input must be digital";
    if (usesStartInput(v.start_method) && v.start_block_input_id == v.start_input_id) {
        return "start block input equals start input";
    }
    return nullptr;
}

const char* checkEngineIndicator(const BaseConfig& cfg) {
    const VehicleConfig& v = cfg.vehicle;
    if (strcmp(v.engine_detection_source, "input") != 0) return nullptr;
    int8_t idx = findInput(cfg, v.engine_input_id);
    if (idx < 0) return "engine indicator input not found";
    const InputConfig& in = cfg.inputs[idx];
    if (in.type == InputType::PULSE_COUNTER && in.pulse.threshold_rpm == 0) {
        return "engine indicator pulse input needs threshold_rpm > 0";
    }
    return nullptr;
}

uint8_t normalize(BaseConfig& cfg, NoteFn note, void* ctx) {
    uint8_t fixes = 0;
    VehicleConfig& v = cfg.vehicle;

    if (!maxTimeInRange(v.starter_max_time_sec)) {
        v.starter_max_time_sec = StarterLimits::MAX_TIME_DEFAULT_SEC;
        report(note, ctx, "starter_max_time_sec out of range -> default", fixes);
    }
    if (!waitInRange(v.wait_after_start_sec)) {
        v.wait_after_start_sec = StarterLimits::WAIT_AFTER_START_DEFAULT_SEC;
        report(note, ctx, "wait_after_start_sec out of range -> default", fixes);
    }
    if (!crankInRange(v.starter_crank_time_sec)) {
        v.starter_crank_time_sec = StarterLimits::CRANK_TIME_DEFAULT_SEC;
        report(note, ctx, "starter_crank_time_sec out of range -> default", fixes);
    }
    if (v.starter_crank_time_sec > static_cast<float>(v.starter_max_time_sec)) {
        v.starter_crank_time_sec = static_cast<float>(v.starter_max_time_sec);
        report(note, ctx, "starter_crank_time_sec > starter_max_time_sec -> clamped", fixes);
    }
    if (v.starter_retry_pause_sec > StarterLimits::RETRY_PAUSE_MAX_SEC) {
        v.starter_retry_pause_sec = StarterLimits::RETRY_PAUSE_DEFAULT_SEC;
        report(note, ctx, "starter_retry_pause_sec out of range -> default", fixes);
    }
    if (!attemptsInRange(v.starter_attempts)) {
        v.starter_attempts = StarterLimits::ATTEMPTS_DEFAULT;
        report(note, ctx, "starter_attempts out of range -> default", fixes);
    }
    if (!voltageInRange(v.engine_running_voltage_threshold) ||
        !voltageInRange(v.engine_stopped_voltage_threshold) ||
        v.engine_running_voltage_threshold <= v.engine_stopped_voltage_threshold) {
        v.engine_running_voltage_threshold = StarterLimits::RUNNING_VOLTAGE_DEFAULT;
        v.engine_stopped_voltage_threshold = StarterLimits::STOPPED_VOLTAGE_DEFAULT;
        report(note, ctx, "engine voltage thresholds invalid -> defaults", fixes);
    }

    for (uint8_t i = 0; i < HardwareLimits::INPUTS; i++) {
        if (cfg.inputs[i].pulse.pulses_per_rev == 0) {
            cfg.inputs[i].pulse.pulses_per_rev = 1;
            report(note, ctx, "pulses_per_rev 0 -> 1", fixes);
        }
    }

    if (checkStartInput(cfg, v.start_method, v.start_input_id) != nullptr) {
        v.start_method = StartMethod::Timed;
        report(note, ctx, "start input unusable -> start_method timed", fixes);
    }

    BatterySaverConfig& bs = cfg.battery_saver;
    if (!voltageInRange(bs.voltage_start_threshold) || !voltageInRange(bs.voltage_abort_threshold) ||
        bs.voltage_start_threshold <= bs.voltage_abort_threshold) {
        BatterySaverConfig def;
        bs.voltage_start_threshold = def.voltage_start_threshold;
        bs.voltage_abort_threshold = def.voltage_abort_threshold;
        report(note, ctx, "battery_saver thresholds invalid -> defaults", fixes);
    }
    return fixes;
}

const char* validate(const BaseConfig& cfg) {
    const VehicleConfig& v = cfg.vehicle;
    if (!maxTimeInRange(v.starter_max_time_sec)) return "starter_max_time_sec out of range";
    if (!waitInRange(v.wait_after_start_sec)) return "wait_after_start_sec out of range";
    if (!crankInRange(v.starter_crank_time_sec)) return "starter_crank_time_sec out of range";
    if (v.starter_retry_pause_sec > StarterLimits::RETRY_PAUSE_MAX_SEC) return "starter_retry_pause_sec out of range";
    if (!attemptsInRange(v.starter_attempts)) return "starter_attempts out of range";
    if (v.start_method == StartMethod::Timed &&
        v.starter_crank_time_sec > static_cast<float>(v.starter_max_time_sec)) {
        return "starter_crank_time_sec > starter_max_time_sec";
    }
    if (!voltageInRange(v.engine_running_voltage_threshold) || !voltageInRange(v.engine_stopped_voltage_threshold)) {
        return "engine voltage threshold out of range";
    }
    if (v.engine_running_voltage_threshold <= v.engine_stopped_voltage_threshold) {
        return "engine running threshold must be above stopped threshold";
    }
    for (uint8_t i = 0; i < HardwareLimits::INPUTS; i++) {
        if (cfg.inputs[i].pulse.pulses_per_rev == 0) return "pulses_per_rev must be >= 1";
    }
    if (const char* r = checkStartInput(cfg, v.start_method, v.start_input_id)) return r;
    if (const char* r = checkBlockInput(cfg)) return r;
    if (const char* r = checkEngineIndicator(cfg)) return r;

    const BatterySaverConfig& bs = cfg.battery_saver;
    if (bs.voltage_start_threshold <= bs.voltage_abort_threshold) {
        return "battery_saver start threshold must be above abort threshold";
    }
    return nullptr;
}

} // namespace vehicle_rules
