// src/config/Config.BaseSax.cpp
#include "config/internal/BaseConfigJsonIo.h"
#include "config/internal/JsonScalar.h"

#include "json/JsonParseFile.h"
#include "JsonListener.h"
#include "JsonStreamingParser.h"

#include "common/Constants.h"
#include "common/Logger.h"
#include "config/ConfigTypes.h"
#include "config/VehicleRules.h"

#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace config_internal {

namespace {

bool streq(const char* a, const char* b) { return a && b && strcmp(a, b) == 0; }







void initBaseLikeFillBasic(BaseConfig& t) {
    for (uint8_t i = 0; i < HardwareLimits::SENSORS; i++) {
        t.sensors[i].id = 0;
        t.sensors[i].coeff = 0.1f;
        t.sensors[i].name[0] = '\0';
        t.sensors[i].rom[0] = '\0';
    }
    for (uint8_t i = 0; i < HardwareLimits::INPUTS; i++) {
        t.inputs[i].id = Pin::INPUT_IDS[i];
        t.inputs[i].enabled = true;
        t.inputs[i].type = InputType::DIGITAL;
        t.inputs[i].active_state = LOW;
        t.inputs[i].name[0] = '\0';
        t.inputs[i].pulse.pulses_per_rev = 1;
        t.inputs[i].pulse.threshold_rpm = 0;
    }
    t.input_triggers_count = 0;
    t.temperature_triggers_count = 0;
    t.schedule_triggers_count = 0;
    t.setup_required = false;
    t.time = TimeConfig{}; // disabled + empty NTP until JSON `time` (or factory DefaultConfig) fills it
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
    TimeCfg,
    InTrigArr,
    InTrigObj,
    TempTrigArr,
    TempTrigObj,
    SchedTrigArr,
    SchedTrigObj,
};

class BaseConfigListener final : public JsonListener {
public:
    explicit BaseConfigListener(BaseConfig& out) : t_(&out) {}

    bool failed() const { return failed_; }
    bool rootNotObject() const { return rootNotObject_; }
    bool sawRootObject() const { return sawRootObject_; }
    uint8_t extraTrimmed() const { return extraTrimmed_; }

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
        extraTrimmed_ = 0;
        seenActive_ = false;
        hasAdcCalibrate_ = false;
        adcCalibrateVoltage_ = 0.0f;
        trig_ = TriggerConfig{};
        ttrig_ = TempTriggerConfig{};
        strig_ = ScheduleTriggerConfig{};
    }

    bool hasAdcCalibrate() const { return hasAdcCalibrate_; }
    float adcCalibrateVoltage() const { return adcCalibrateVoltage_; }

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
                else if (streq(pending_, "time")) push(St::TimeCfg);
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
                    extraTrimmed_++;
                    push(St::Junk);
                    junkNest_ = 1;
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
                    extraTrimmed_++;
                    push(St::Junk);
                    junkNest_ = 1;
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
            case St::SchedTrigArr: {
                strig_ = ScheduleTriggerConfig{};
                push(St::SchedTrigObj);
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
            } else {
                extraTrimmed_++;
            }
        }
        if (closing == St::TempTrigObj) {
            if (t_->temperature_triggers_count < Limits::MAX_TRIGGERS) {
                t_->temperature_triggers[t_->temperature_triggers_count++] = ttrig_;
            } else {
                extraTrimmed_++;
            }
        }
        if (closing == St::SchedTrigObj) {
            if (t_->schedule_triggers_count < Limits::MAX_SCHEDULE_TRIGGERS) {
                t_->schedule_triggers[t_->schedule_triggers_count++] = strig_;
            } else {
                extraTrimmed_++;
            }
        }
        pop();
    }

    void startArray() override {
        if (failed_) return;
        if (sp_ < 0) {
            rootNotObject_ = true;
            failed_ = true;
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
            } else if (streq(pending_, "schedule_triggers")) {
                push(St::SchedTrigArr);
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
                    t_->wifi.ap_timeout_sec = json_scalar::parseU16(v, 0);
                else if (streq(pending_, "ap_timeout_enabled"))
                    t_->wifi.ap_timeout_enabled = json_scalar::parseBool(v, false);
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
                    t_->mqtt.port = json_scalar::parseU16(v, 1883);
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
                    t_->mqtt.publish_interval_sec = json_scalar::parseU16(v, Defaults::MQTT_PUBLISH_INTERVAL_SEC);
                break;
            case St::Vehicle:
                if (streq(pending_, "adc_voltage_coeff")) t_->vehicle.adc_voltage_coeff = json_scalar::parseF(v, 1.0f);
                else if (streq(pending_, "adc_calibrate_voltage")) {
                    // Ephemeral: not stored in BaseConfig; used only on posted apply.
                    hasAdcCalibrate_ = true;
                    adcCalibrateVoltage_ = json_scalar::parseF(v, 0.0f);
                } else if (streq(pending_, "starter_max_time_sec"))
                    t_->vehicle.starter_max_time_sec = json_scalar::parseU16(v, StarterLimits::MAX_TIME_DEFAULT_SEC);
                else if (streq(pending_, "wait_after_start_sec"))
                    t_->vehicle.wait_after_start_sec = json_scalar::parseU16(v, StarterLimits::WAIT_AFTER_START_DEFAULT_SEC);
                else if (streq(pending_, "engine_running_voltage_threshold"))
                    t_->vehicle.engine_running_voltage_threshold =
                        json_scalar::parseF(v, StarterLimits::RUNNING_VOLTAGE_DEFAULT);
                else if (streq(pending_, "engine_stopped_voltage_threshold"))
                    t_->vehicle.engine_stopped_voltage_threshold =
                        json_scalar::parseF(v, StarterLimits::STOPPED_VOLTAGE_DEFAULT);
                else if (streq(pending_, "starter_relay_id"))
                    t_->vehicle.starter_relay_id = json_scalar::parseU16(v, 2002);
                else if (streq(pending_, "start_method"))
                    t_->vehicle.start_method = vehicle_rules::startMethodFromString(v, StartMethod::Timed);
                else if (streq(pending_, "start_input_id"))
                    t_->vehicle.start_input_id = json_scalar::parseU16(v, 0);
                else if (streq(pending_, "starter_crank_time_sec"))
                    t_->vehicle.starter_crank_time_sec = json_scalar::parseF(v, StarterLimits::CRANK_TIME_DEFAULT_SEC);
                else if (streq(pending_, "starter_retry_pause_sec"))
                    t_->vehicle.starter_retry_pause_sec = json_scalar::parseU16(v, StarterLimits::RETRY_PAUSE_DEFAULT_SEC);
                else if (streq(pending_, "starter_attempts"))
                    t_->vehicle.starter_attempts = json_scalar::parseU8(v, StarterLimits::ATTEMPTS_DEFAULT);
                else if (streq(pending_, "start_block_input_id"))
                    t_->vehicle.start_block_input_id = json_scalar::parseU16(v, 0);
                else if (streq(pending_, "engine_detection_source"))
                    strlcpy(t_->vehicle.engine_detection_source, v ? v : "voltage",
                            sizeof t_->vehicle.engine_detection_source);
                else if (streq(pending_, "engine_input_id"))
                    t_->vehicle.engine_input_id = json_scalar::parseU16(v, 0);
                break;
            case St::SensorObj:
                if (si_ == 255 || si_ >= HardwareLimits::SENSORS) break;
                if (streq(pending_, "id")) t_->sensors[si_].id = json_scalar::parseU16(v, 0);
                else if (streq(pending_, "coeff"))
                    t_->sensors[si_].coeff = json_scalar::parseF(v, 0.1f);
                else if (streq(pending_, "name"))
                    strlcpy(t_->sensors[si_].name, v ? v : "", sizeof t_->sensors[si_].name);
                else if (streq(pending_, "rom"))
                    strlcpy(t_->sensors[si_].rom, v ? v : "", sizeof t_->sensors[si_].rom);
                break;
            case St::InputObj:
                if (ii_ == 255 || ii_ >= HardwareLimits::INPUTS) break;
                // "id" is ignored: input ids are hardware identity (Pin::INPUT_IDS), set in initBaseLikeFillBasic.
                if (streq(pending_, "enabled"))
                    t_->inputs[ii_].enabled = json_scalar::parseBool(v, true);
                else if (streq(pending_, "type"))
                    t_->inputs[ii_].type = (json_scalar::parseU8(v, 0) == static_cast<uint8_t>(InputType::PULSE_COUNTER))
                                               ? InputType::PULSE_COUNTER
                                               : InputType::DIGITAL;
                else if (streq(pending_, "name"))
                    strlcpy(t_->inputs[ii_].name, v ? v : "", sizeof t_->inputs[ii_].name);
                else if (streq(pending_, "active_state")) {
                    t_->inputs[ii_].active_state = json_scalar::parseBool(v, true);
                    seenActive_ = true;
                }
                break;
            case St::PulseObj:
                if (ii_ == 255 || ii_ >= HardwareLimits::INPUTS) break;
                if (streq(pending_, "pulses_per_rev"))
                    t_->inputs[ii_].pulse.pulses_per_rev = json_scalar::parseU16(v, 1);
                else if (streq(pending_, "threshold_rpm"))
                    t_->inputs[ii_].pulse.threshold_rpm = json_scalar::parseU16(v, 0);
                break;
            case St::Thermostat:
                if (streq(pending_, "enabled")) t_->thermostat.enabled = json_scalar::parseBool(v, false);
                else if (streq(pending_, "sensor_id"))
                    t_->thermostat.sensor_id = json_scalar::parseU16(v, 0);
                else if (streq(pending_, "comparison"))
                    strlcpy(t_->thermostat.comparison, v ? v : "above", sizeof t_->thermostat.comparison);
                else if (streq(pending_, "lower_threshold"))
                    t_->thermostat.lower_threshold = json_scalar::parseF(v, 5.0f);
                else if (streq(pending_, "upper_threshold"))
                    t_->thermostat.upper_threshold = json_scalar::parseF(v, 10.0f);
                else if (streq(pending_, "program_id_lower"))
                    t_->thermostat.program_id_lower = json_scalar::parseU8(v, 0);
                else if (streq(pending_, "program_id_upper"))
                    t_->thermostat.program_id_upper = json_scalar::parseU8(v, 0);
                break;
            case St::BatterySaver:
                if (streq(pending_, "enabled")) t_->battery_saver.enabled = json_scalar::parseBool(v, false);
                else if (streq(pending_, "voltage_start_threshold"))
                    t_->battery_saver.voltage_start_threshold = json_scalar::parseF(v, 11.8f);
                else if (streq(pending_, "voltage_abort_threshold"))
                    t_->battery_saver.voltage_abort_threshold = json_scalar::parseF(v, 10.5f);
                else if (streq(pending_, "hysteresis"))
                    t_->battery_saver.hysteresis = json_scalar::parseF(v, 0.5f);
                else if (streq(pending_, "min_low_voltage_duration_sec"))
                    t_->battery_saver.min_low_voltage_duration_sec = json_scalar::parseU16(v, 180);
                else if (streq(pending_, "min_time_between_attempts_sec"))
                    t_->battery_saver.min_time_between_attempts_sec = json_scalar::parseU16(v, 1800);
                else if (streq(pending_, "max_attempts_per_day"))
                    t_->battery_saver.max_attempts_per_day = json_scalar::parseU8(v, 3);
                else if (streq(pending_, "program_id"))
                    t_->battery_saver.program_id = json_scalar::parseU8(v, 0);
                break;
            case St::TimeCfg:
                if (streq(pending_, "enabled")) t_->time.enabled = json_scalar::parseBool(v, true);
                else if (streq(pending_, "ntp_server"))
                    strlcpy(t_->time.ntp_server, v ? v : "pool.ntp.org", sizeof t_->time.ntp_server);
                else if (streq(pending_, "tz_offset_hours"))
                    t_->time.tz_offset_hours = static_cast<int8_t>(json_scalar::parseI16(v, 3));
                else if (streq(pending_, "sync_interval_hours"))
                    t_->time.sync_interval_hours = json_scalar::parseU32(v, Defaults::TIME_SYNC_INTERVAL_HOURS);
                break;
            case St::InTrigObj:
                if (streq(pending_, "id")) trig_.id = json_scalar::parseU16(v, 0);
                else if (streq(pending_, "enabled"))
                    trig_.enabled = json_scalar::parseBool(v, false);
                else if (streq(pending_, "input_id"))
                    trig_.input_id = json_scalar::parseU16(v, 0);
                else if (streq(pending_, "trigger_level"))
                    trig_.trigger_level = json_scalar::parseU8(v, 1);
                else if (streq(pending_, "program_id"))
                    trig_.program_id = json_scalar::parseU8(v, 0);
                break;
            case St::TempTrigObj:
                if (streq(pending_, "enabled")) ttrig_.enabled = json_scalar::parseBool(v, false);
                else if (streq(pending_, "id")) ttrig_.id = json_scalar::parseU16(v, 0);
                else if (streq(pending_, "sensor_id"))
                    ttrig_.sensor_id = json_scalar::parseU16(v, 0);
                else if (streq(pending_, "comparison"))
                    strlcpy(ttrig_.comparison, v ? v : "above", sizeof ttrig_.comparison);
                else if (streq(pending_, "threshold"))
                    ttrig_.threshold = json_scalar::parseF(v, 0.0f);
                else if (streq(pending_, "program_id"))
                    ttrig_.program_id = json_scalar::parseU8(v, 0);
                break;
            case St::SchedTrigObj:
                if (streq(pending_, "id")) strig_.id = json_scalar::parseU16(v, 0);
                else if (streq(pending_, "enabled"))
                    strig_.enabled = json_scalar::parseBool(v, false);
                else if (streq(pending_, "hour"))
                    strig_.hour = json_scalar::parseU8(v, 0);
                else if (streq(pending_, "minute"))
                    strig_.minute = json_scalar::parseU8(v, 0);
                else if (streq(pending_, "days_mask"))
                    strig_.days_mask = json_scalar::parseU8(v, 0x7F);
                else if (streq(pending_, "program_id"))
                    strig_.program_id = json_scalar::parseU8(v, 0);
                break;
            case St::Root:
                if (streq(pending_, "setup_required")) t_->setup_required = json_scalar::parseBool(v, false);
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
    // Longest config keys are "engine_running_voltage_threshold" / "..._stopped_..." (32 chars).
    char pending_[40]{};
    uint8_t si_{255};
    uint8_t ii_{255};
    bool seenActive_{false};
    TriggerConfig trig_;
    TempTriggerConfig ttrig_;
    ScheduleTriggerConfig strig_;

    int junkNest_{0};
    uint8_t extraTrimmed_{0};

    bool failed_{false};
    bool rootNotObject_{false};
    bool sawRootObject_{false};

    bool hasAdcCalibrate_{false};
    float adcCalibrateVoltage_{0.0f};
};

} // namespace

bool parseBaseConfigStreamingFromFile(File& file, BaseConfig& cfg, ParseMode mode,
                                      bool* outHasAdcCalibrate,
                                      float* outAdcCalibrateVoltage) {
    BaseConfigListener listener(cfg);
    const bool complete = jsonStreamingParseWholeFile(file, listener);
    if (outHasAdcCalibrate) *outHasAdcCalibrate = listener.hasAdcCalibrate();
    if (outAdcCalibrateVoltage) *outAdcCalibrateVoltage = listener.adcCalibrateVoltage();
    if (listener.rootNotObject() || !listener.sawRootObject() || listener.failed()) return false;
    if (!complete && mode == ParseMode::Strict) return false;
    if (listener.extraTrimmed()) {
        logger.log(LogModule::Config, "config arrays trimmed (%u extra object(s))\n",
                   (unsigned)listener.extraTrimmed());
    }
    return true;
}

bool parseBaseConfigStreamingFromProgmem(BaseConfig& cfg, const char* pgmDoc) {
    BaseConfigListener listener(cfg);
    const bool complete = jsonStreamingParseProgmem(listener, pgmDoc);
    if (listener.rootNotObject() || !listener.sawRootObject() || listener.failed()) return false;
    if (listener.extraTrimmed()) {
        logger.log(LogModule::Config, "config arrays trimmed (%u extra object(s))\n",
                   (unsigned)listener.extraTrimmed());
    }
    return complete;
}

} // namespace config_internal
