// src/config/Config.BaseSax.cpp
#include "config/internal/BaseConfigJsonIo.h"

#include "json/Json.ParseFile.h"
#include "JsonListener.h"
#include "JsonStreamingParser.h"

#include "common/Constants.h"
#include "config/ConfigTypes.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

namespace config_internal {

namespace {

bool streq(const char* a, const char* b) { return a && b && strcmp(a, b) == 0; }

bool parseBool(const char* v, bool def) {
    if (!v || !*v) return def;
    if (strcmp(v, "true") == 0 || strcmp(v, "1") == 0) return true;
    if (strcmp(v, "false") == 0 || strcmp(v, "0") == 0) return false;
    return def;
}

uint32_t parseU32(const char* v, uint32_t def) {
    if (!v || !*v) return def;
    char* end = nullptr;
    unsigned long n = strtoul(v, &end, 10);
    if (!end || *end != '\0') return def;
    return static_cast<uint32_t>(n);
}

uint16_t parseU16(const char* v, uint16_t def) {
    uint32_t n = parseU32(v, def);
    if (n > 65535u) return 65535;
    return static_cast<uint16_t>(n);
}

uint8_t parseU8(const char* v, uint8_t def) {
    uint32_t n = parseU32(v, def);
    if (n > 255u) return 255;
    return static_cast<uint8_t>(n);
}

float parseF(const char* v, float def) {
    if (!v || !*v) return def;
    return strtof(v, nullptr);
}

void initBaseLikeFillBasic(BaseConfig& t) {
    for (uint8_t i = 0; i < HardwareLimits::SENSORS; i++) {
        t.sensors[i].id = 0;
        t.sensors[i].coeff = 0.1f;
        t.sensors[i].name[0] = '\0';
        t.sensors[i].rom[0] = '\0';
    }
    for (uint8_t i = 0; i < HardwareLimits::INPUTS; i++) {
        t.inputs[i].id = static_cast<uint16_t>(i + 1);
        t.inputs[i].enabled = true;
        t.inputs[i].type = InputType::DIGITAL;
        t.inputs[i].active_state = LOW;
        t.inputs[i].name[0] = '\0';
        t.inputs[i].pulse.pulses_per_rev = 1;
        t.inputs[i].pulse.threshold_rpm = 0;
    }
    t.input_triggers_count = 0;
    t.temperature_triggers_count = 0;
    t.setup_required = false;
}

enum class St : uint8_t {
    Root,
    Junk,
    Wifi,
    Gsm,
    Mqtt,
    Vehicle,
    SensorsArr,
    SensorObj,
    InputsArr,
    InputObj,
    PulseObj,
    Thermostat,
    BatterySaver,
    InTrigArr,
    InTrigObj,
    TempTrigArr,
    TempTrigObj,
};

class BaseConfigListener final : public JsonListener {
public:
    explicit BaseConfigListener(BaseConfig& out) : t_(&out) {}

    bool failed() const { return failed_; }
    bool rootNotObject() const { return rootNotObject_; }
    bool sawRootObject() const { return sawRootObject_; }

    void whitespace(char) override {}
    void startDocument() override {
        failed_ = false;
        rootNotObject_ = false;
        sawRootObject_ = false;
        sp_ = -1;
        pending_[0] = '\0';
        si_ = 255;
        ii_ = 255;
        junkNest_ = 0;
        seenActive_ = false;
        trig_ = TriggerConfig{};
        ttrig_ = TempTriggerConfig{};
    }

    void endDocument() override {}

    void startObject() override {
        if (failed_) return;
        if (sp_ < 0) {
            initBaseLikeFillBasic(*t_);
            sawRootObject_ = true;
            push(St::Root);
            return;
        }
        if (top() == St::Junk) {
            junkNest_++;
            return;
        }
        switch (top()) {
            case St::Root: {
                if (streq(pending_, "wifi")) push(St::Wifi);
                else if (streq(pending_, "gsm")) push(St::Gsm);
                else if (streq(pending_, "mqtt")) push(St::Mqtt);
                else if (streq(pending_, "vehicle")) push(St::Vehicle);
                else if (streq(pending_, "thermostat")) push(St::Thermostat);
                else if (streq(pending_, "battery_saver")) push(St::BatterySaver);
                else {
                    push(St::Junk);
                    junkNest_ = 1;
                }
                break;
            }
            case St::SensorsArr: {
                if (si_ == 255) si_ = 0;
                else if (si_ + 1 < HardwareLimits::SENSORS)
                    si_++;
                else {
                    failed_ = true;
                    break;
                }
                push(St::SensorObj);
                break;
            }
            case St::InputsArr: {
                if (ii_ == 255)
                    ii_ = 0;
                else if (ii_ + 1 < HardwareLimits::INPUTS)
                    ii_++;
                else {
                    failed_ = true;
                    break;
                }
                push(St::InputObj);
                seenActive_ = false;
                break;
            }
            case St::InputObj: {
                if (streq(pending_, "pulse")) push(St::PulseObj);
                break;
            }
            case St::InTrigArr: {
                trig_ = TriggerConfig{};
                push(St::InTrigObj);
                break;
            }
            case St::TempTrigArr: {
                ttrig_ = TempTriggerConfig{};
                push(St::TempTrigObj);
                break;
            }
            default:
                break;
        }
    }

    void endObject() override {
        if (failed_) return;
        if (sp_ < 0) return;
        if (top() == St::Junk) {
            junkNest_--;
            if (junkNest_ == 0) pop();
            return;
        }
        const St closing = top();
        if (closing == St::InputObj && ii_ != 255) {
            InputConfig& ic = t_->inputs[ii_];
            if (ic.type == InputType::DIGITAL && !seenActive_) ic.active_state = true;
        }
        if (closing == St::InTrigObj) {
            if (t_->input_triggers_count < Limits::MAX_TRIGGERS) {
                t_->input_triggers[t_->input_triggers_count++] = trig_;
            }
        }
        if (closing == St::TempTrigObj) {
            if (t_->temperature_triggers_count < Limits::MAX_TRIGGERS) {
                t_->temperature_triggers[t_->temperature_triggers_count++] = ttrig_;
            }
        }
        pop();
    }

    void startArray() override {
        if (failed_) return;
        if (sp_ < 0) {
            rootNotObject_ = true;
            return;
        }
        if (top() == St::Junk) {
            junkNest_++;
            return;
        }
        if (top() == St::Root) {
            if (streq(pending_, "sensors")) {
                si_ = 255;
                push(St::SensorsArr);
            } else if (streq(pending_, "inputs")) {
                ii_ = 255;
                push(St::InputsArr);
            } else if (streq(pending_, "input_triggers")) {
                push(St::InTrigArr);
            } else if (streq(pending_, "temperature_triggers")) {
                push(St::TempTrigArr);
            }
        }
    }

    void endArray() override {
        if (failed_) return;
        if (sp_ < 0) return;
        if (top() == St::Junk) {
            junkNest_--;
            if (junkNest_ == 0) pop();
            return;
        }
        pop();
    }

    void key(const char* k) override {
        if (failed_) return;
        strlcpy(pending_, k ? k : "", sizeof pending_);
    }

    void value(const char* val) override {
        if (failed_) return;
        const char* v = val;
        if (top() == St::Junk) return;
        if (v && strcmp(v, "null") == 0) return;
        switch (top()) {
            case St::Wifi:
                if (streq(pending_, "ssid")) strlcpy(t_->wifi.ssid, v ? v : "", sizeof t_->wifi.ssid);
                else if (streq(pending_, "password"))
                    strlcpy(t_->wifi.password, v ? v : "", sizeof t_->wifi.password);
                else if (streq(pending_, "ap_timeout_sec"))
                    t_->wifi.ap_timeout_sec = parseU16(v, 0);
                else if (streq(pending_, "ap_timeout_enabled"))
                    t_->wifi.ap_timeout_enabled = parseBool(v, false);
                break;
            case St::Gsm:
                if (streq(pending_, "apn")) strlcpy(t_->gsm.apn, v ? v : "", sizeof t_->gsm.apn);
                else if (streq(pending_, "apn_user"))
                    strlcpy(t_->gsm.apn_user, v ? v : "", sizeof t_->gsm.apn_user);
                else if (streq(pending_, "apn_pass"))
                    strlcpy(t_->gsm.apn_pass, v ? v : "", sizeof t_->gsm.apn_pass);
                else if (streq(pending_, "owner_phone"))
                    strlcpy(t_->gsm.owner_phone, v ? v : "", sizeof t_->gsm.owner_phone);
                else if (streq(pending_, "dtmf_password"))
                    strlcpy(t_->gsm.dtmf_password, v ? v : "", sizeof t_->gsm.dtmf_password);
                break;
            case St::Mqtt:
                if (streq(pending_, "broker")) strlcpy(t_->mqtt.broker, v ? v : "", sizeof t_->mqtt.broker);
                else if (streq(pending_, "port"))
                    t_->mqtt.port = parseU16(v, 1883);
                else if (streq(pending_, "client_id"))
                    strlcpy(t_->mqtt.client_id, v ? v : "", sizeof t_->mqtt.client_id);
                else if (streq(pending_, "user"))
                    strlcpy(t_->mqtt.user, v ? v : "", sizeof t_->mqtt.user);
                else if (streq(pending_, "pass"))
                    strlcpy(t_->mqtt.pass, v ? v : "", sizeof t_->mqtt.pass);
                else if (streq(pending_, "topic_prefix")) {
                    strlcpy(t_->mqtt.topic_prefix, v ? v : "", sizeof t_->mqtt.topic_prefix);
                    // Drop trailing '/' so joinTopic_ can append suffixes cleanly.
                    size_t n = strlen(t_->mqtt.topic_prefix);
                    while (n > 0 && t_->mqtt.topic_prefix[n - 1] == '/') {
                        t_->mqtt.topic_prefix[--n] = '\0';
                    }
                } else if (streq(pending_, "publish_interval_sec"))
                    t_->mqtt.publish_interval_sec = parseU16(v, 30);
                break;
            case St::Vehicle:
                if (streq(pending_, "adc_voltage_coeff")) t_->vehicle.adc_voltage_coeff = parseF(v, 1.0f);
                else if (streq(pending_, "starter_max_time_sec"))
                    t_->vehicle.starter_max_time_sec = parseU16(v, 5);
                else if (streq(pending_, "wait_after_start_sec"))
                    t_->vehicle.wait_after_start_sec = parseU16(v, 10);
                else if (streq(pending_, "engine_running_voltage_threshold"))
                    t_->vehicle.engine_running_voltage_threshold = parseF(v, 13.2f);
                else if (streq(pending_, "engine_stopped_voltage_threshold"))
                    t_->vehicle.engine_stopped_voltage_threshold = parseF(v, 12.8f);
                else if (streq(pending_, "starter_relay_id"))
                    t_->vehicle.starter_relay_id = parseU16(v, 2002);
                else if (streq(pending_, "engine_detection_source"))
                    strlcpy(t_->vehicle.engine_detection_source, v ? v : "voltage",
                            sizeof t_->vehicle.engine_detection_source);
                else if (streq(pending_, "engine_input_id"))
                    t_->vehicle.engine_input_id = parseU16(v, 0);
                break;
            case St::SensorObj:
                if (si_ == 255 || si_ >= HardwareLimits::SENSORS) break;
                if (streq(pending_, "id")) t_->sensors[si_].id = parseU16(v, 0);
                else if (streq(pending_, "coeff"))
                    t_->sensors[si_].coeff = parseF(v, 0.1f);
                else if (streq(pending_, "name"))
                    strlcpy(t_->sensors[si_].name, v ? v : "", sizeof t_->sensors[si_].name);
                else if (streq(pending_, "rom"))
                    strlcpy(t_->sensors[si_].rom, v ? v : "", sizeof t_->sensors[si_].rom);
                break;
            case St::InputObj:
                if (ii_ == 255 || ii_ >= HardwareLimits::INPUTS) break;
                if (streq(pending_, "id")) t_->inputs[ii_].id = parseU16(v, 0);
                else if (streq(pending_, "enabled"))
                    t_->inputs[ii_].enabled = parseBool(v, true);
                else if (streq(pending_, "type"))
                    t_->inputs[ii_].type = static_cast<InputType>(parseU8(v, 0));
                else if (streq(pending_, "name"))
                    strlcpy(t_->inputs[ii_].name, v ? v : "", sizeof t_->inputs[ii_].name);
                else if (streq(pending_, "active_state")) {
                    t_->inputs[ii_].active_state = parseBool(v, true);
                    seenActive_ = true;
                }
                break;
            case St::PulseObj:
                if (ii_ == 255 || ii_ >= HardwareLimits::INPUTS) break;
                if (streq(pending_, "pulses_per_rev"))
                    t_->inputs[ii_].pulse.pulses_per_rev = parseU16(v, 1);
                else if (streq(pending_, "threshold_rpm"))
                    t_->inputs[ii_].pulse.threshold_rpm = parseU16(v, 0);
                break;
            case St::Thermostat:
                if (streq(pending_, "enabled")) t_->thermostat.enabled = parseBool(v, false);
                else if (streq(pending_, "sensor_id"))
                    t_->thermostat.sensor_id = parseU16(v, 0);
                else if (streq(pending_, "comparison"))
                    strlcpy(t_->thermostat.comparison, v ? v : "above", sizeof t_->thermostat.comparison);
                else if (streq(pending_, "lower_threshold"))
                    t_->thermostat.lower_threshold = parseF(v, 5.0f);
                else if (streq(pending_, "upper_threshold"))
                    t_->thermostat.upper_threshold = parseF(v, 10.0f);
                else if (streq(pending_, "program_id_lower"))
                    t_->thermostat.program_id_lower = parseU8(v, 0);
                else if (streq(pending_, "program_id_upper"))
                    t_->thermostat.program_id_upper = parseU8(v, 0);
                break;
            case St::BatterySaver:
                if (streq(pending_, "enabled")) t_->battery_saver.enabled = parseBool(v, false);
                else if (streq(pending_, "voltage_start_threshold"))
                    t_->battery_saver.voltage_start_threshold = parseF(v, 11.8f);
                else if (streq(pending_, "voltage_abort_threshold"))
                    t_->battery_saver.voltage_abort_threshold = parseF(v, 10.5f);
                else if (streq(pending_, "hysteresis"))
                    t_->battery_saver.hysteresis = parseF(v, 0.5f);
                else if (streq(pending_, "min_low_voltage_duration_sec"))
                    t_->battery_saver.min_low_voltage_duration_sec = parseU16(v, 180);
                else if (streq(pending_, "min_time_between_attempts_sec"))
                    t_->battery_saver.min_time_between_attempts_sec = parseU16(v, 1800);
                else if (streq(pending_, "max_attempts_per_day"))
                    t_->battery_saver.max_attempts_per_day = parseU8(v, 3);
                else if (streq(pending_, "program_id"))
                    t_->battery_saver.program_id = parseU8(v, 0);
                break;
            case St::InTrigObj:
                if (streq(pending_, "id")) trig_.id = parseU16(v, 0);
                else if (streq(pending_, "enabled"))
                    trig_.enabled = parseBool(v, false);
                else if (streq(pending_, "input_id"))
                    trig_.input_id = parseU16(v, 0);
                else if (streq(pending_, "trigger_level"))
                    trig_.trigger_level = parseU8(v, 1);
                else if (streq(pending_, "program_id"))
                    trig_.program_id = parseU8(v, 0);
                break;
            case St::TempTrigObj:
                if (streq(pending_, "enabled")) ttrig_.enabled = parseBool(v, false);
                else if (streq(pending_, "id")) ttrig_.id = parseU16(v, 0);
                else if (streq(pending_, "sensor_id"))
                    ttrig_.sensor_id = parseU16(v, 0);
                else if (streq(pending_, "comparison"))
                    strlcpy(ttrig_.comparison, v ? v : "above", sizeof ttrig_.comparison);
                else if (streq(pending_, "threshold"))
                    ttrig_.threshold = parseF(v, 0.0f);
                else if (streq(pending_, "program_id"))
                    ttrig_.program_id = parseU8(v, 0);
                break;
            case St::Root:
                if (streq(pending_, "setup_required")) t_->setup_required = parseBool(v, false);
                break;
            default:
                break;
        }
    }

private:
    void push(St s) {
        if (sp_ + 1 >= 24) {
            failed_ = true;
            return;
        }
        stk_[++sp_] = s;
    }
    St top() const { return stk_[sp_]; }
    void pop() {
        if (sp_ >= 0) sp_--;
    }

    BaseConfig* t_;
    St stk_[24]{};
    int sp_{-1};
    // Longest config key is "engine_detection_source" (23 chars) + '\0'.
    char pending_[24]{};
    uint8_t si_{255};
    uint8_t ii_{255};
    bool seenActive_{false};
    TriggerConfig trig_;
    TempTriggerConfig ttrig_;

    int junkNest_{0};

    bool failed_{false};
    bool rootNotObject_{false};
    bool sawRootObject_{false};
};

} // namespace

bool parseBaseConfigStreamingFromFile(File& file, BaseConfig& cfg) {
    BaseConfigListener listener(cfg);
    jsonStreamingParseWholeFile(file, listener);
    if (listener.rootNotObject()) return true;
    if (!listener.sawRootObject()) return false;
    return !listener.failed();
}

bool parseBaseConfigStreamingFromProgmem(BaseConfig& cfg, const char* pgmDoc) {
    BaseConfigListener listener(cfg);
    jsonStreamingParseProgmem(listener, pgmDoc);
    if (listener.rootNotObject()) return true;
    if (!listener.sawRootObject()) return false;
    return !listener.failed();
}

} // namespace config_internal
