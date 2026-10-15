// src/config/Config.BaseJsonEmit.cpp
#include "config/internal/BaseConfigJsonIo.h"

#include "common/Constants.h"
#include "common/JsonEscape.h"
#include "common/Utils.h"
#include "config/VehicleRules.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace config_internal {

namespace {

/// Arduino Print(float) defaults to 2 decimals — small coeffs (e.g. 0.004) become 0.00.
void printJsonFloat(Print& p, float v) {
    if (!isfinite(v)) {
        p.print("0");
        return;
    }
    char buf[32];
    snprintf(buf, sizeof buf, "%.8g", static_cast<double>(v));
    p.print(buf);
}


inline void comma(Print& p, bool* needComma) {
    if (*needComma) p.print(',');
    *needComma = true;
}

const char* findSensorNameById(const BaseConfig& cfg, uint16_t sensorId) {
    if (sensorId == 0) return "";
    for (uint8_t i = 0; i < HardwareLimits::SENSORS; i++) {
        const SensorConfig& s = cfg.sensors[i];
        if (s.id == sensorId && s.name[0] != '\0') return s.name;
    }
    return "";
}

struct ForwardCountPrint final : public Print {
    Print& d;
    size_t n = 0;
    explicit ForwardCountPrint(Print& downstream) : d(downstream) {}
    size_t write(uint8_t b) override {
        const size_t w = d.write(b);
        n += w;
        return w;
    }
    size_t write(const uint8_t* buffer, size_t size) override {
        const size_t w = d.write(buffer, size);
        n += w;
        return w;
    }
};

} // namespace

size_t serializeBaseConfigToPrint(const BaseConfig& cfg, Print& out) {
    ForwardCountPrint fc(out);
    Print& o = fc;
    bool c = false;
    o.print('{');

    comma(o, &c), o.print("\"setup_required\":"),
        cfg.setup_required ? o.print("true") : o.print("false");

    comma(o, &c), o.print("\"wifi\":");
    o.print('{'), c = false;
    comma(o, &c), o.print("\"ssid\":"), json_escape::writeQuoted(o, cfg.wifi.ssid);
    comma(o, &c), o.print("\"password\":"), json_escape::writeQuoted(o, cfg.wifi.password);
    comma(o, &c), o.print("\"ap_timeout_sec\":"), o.print(cfg.wifi.ap_timeout_sec);
    comma(o, &c), o.print("\"ap_timeout_enabled\":"), cfg.wifi.ap_timeout_enabled ? o.print("true") : o.print("false");
    o.print('}');

    comma(o, &c), o.print("\"gsm\":");
    o.print('{'), c = false;
    comma(o, &c), o.print("\"apn\":"), json_escape::writeQuoted(o, cfg.gsm.apn);
    comma(o, &c), o.print("\"apn_user\":"), json_escape::writeQuoted(o, cfg.gsm.apn_user);
    comma(o, &c), o.print("\"apn_pass\":"), json_escape::writeQuoted(o, cfg.gsm.apn_pass);
    comma(o, &c), o.print("\"owner_phone\":"), json_escape::writeQuoted(o, cfg.gsm.owner_phone);
    comma(o, &c), o.print("\"dtmf_password\":"), json_escape::writeQuoted(o, cfg.gsm.dtmf_password);
    o.print('}');

    comma(o, &c), o.print("\"mqtt\":");
    o.print('{'), c = false;
    comma(o, &c), o.print("\"broker\":"), json_escape::writeQuoted(o, cfg.mqtt.broker);
    comma(o, &c), o.print("\"port\":"), o.print(cfg.mqtt.port);
    comma(o, &c), o.print("\"client_id\":"), json_escape::writeQuoted(o, cfg.mqtt.client_id);
    comma(o, &c), o.print("\"user\":"), json_escape::writeQuoted(o, cfg.mqtt.user);
    comma(o, &c), o.print("\"pass\":"), json_escape::writeQuoted(o, cfg.mqtt.pass);
    comma(o, &c), o.print("\"topic_prefix\":"), json_escape::writeQuoted(o, cfg.mqtt.topic_prefix);
    comma(o, &c), o.print("\"publish_interval_sec\":"), o.print(cfg.mqtt.publish_interval_sec);
    o.print('}');

    comma(o, &c), o.print("\"vehicle\":");
    o.print('{'), c = false;
    comma(o, &c), o.print("\"adc_voltage_coeff\":"), printJsonFloat(o, cfg.vehicle.adc_voltage_coeff);
    comma(o, &c), o.print("\"starter_max_time_sec\":"), o.print(cfg.vehicle.starter_max_time_sec);
    comma(o, &c), o.print("\"wait_after_start_sec\":"), o.print(cfg.vehicle.wait_after_start_sec);
    comma(o, &c), o.print("\"engine_running_voltage_threshold\":"),
        printJsonFloat(o, cfg.vehicle.engine_running_voltage_threshold);
    comma(o, &c), o.print("\"engine_stopped_voltage_threshold\":"),
        printJsonFloat(o, cfg.vehicle.engine_stopped_voltage_threshold);
    comma(o, &c), o.print("\"starter_relay_id\":"), o.print(cfg.vehicle.starter_relay_id);
    comma(o, &c), o.print("\"start_method\":"),
        json_escape::writeQuoted(o, vehicle_rules::startMethodToString(cfg.vehicle.start_method));
    comma(o, &c), o.print("\"start_input_id\":"), o.print(cfg.vehicle.start_input_id);
    comma(o, &c), o.print("\"starter_crank_time_sec\":"), printJsonFloat(o, cfg.vehicle.starter_crank_time_sec);
    comma(o, &c), o.print("\"starter_retry_pause_sec\":"), o.print(cfg.vehicle.starter_retry_pause_sec);
    comma(o, &c), o.print("\"starter_attempts\":"), o.print(cfg.vehicle.starter_attempts);
    comma(o, &c), o.print("\"start_block_input_id\":"), o.print(cfg.vehicle.start_block_input_id);
    comma(o, &c), o.print("\"engine_detection_source\":"), json_escape::writeQuoted(o, cfg.vehicle.engine_detection_source);
    comma(o, &c), o.print("\"engine_input_id\":"), o.print(cfg.vehicle.engine_input_id);
    o.print('}');

    comma(o, &c), o.print("\"sensors\":[");
    for (uint8_t i = 0; i < HardwareLimits::SENSORS; i++) {
        if (i != 0) o.print(',');
        o.print('{'), c = false;
        const SensorConfig& sc = cfg.sensors[i];
        if (sc.id != 0) comma(o, &c), o.print("\"id\":"), o.print(sc.id);
        comma(o, &c), o.print("\"coeff\":"), printJsonFloat(o, sc.coeff);
        if (sc.rom[0] != '\0') comma(o, &c), o.print("\"rom\":"), json_escape::writeQuoted(o, sc.rom);
        if (sc.name[0] != '\0') comma(o, &c), o.print("\"name\":"), json_escape::writeQuoted(o, sc.name);
        o.print('}');
    }
    o.print(']');

    comma(o, &c), o.print("\"inputs\":[");
    for (uint8_t i = 0; i < HardwareLimits::INPUTS; i++) {
        if (i != 0) o.print(',');
        const InputConfig& ic = cfg.inputs[i];
        o.print('{'), c = false;
        if (ic.id != 0) comma(o, &c), o.print("\"id\":"), o.print(ic.id);
        comma(o, &c), o.print("\"enabled\":"), ic.enabled ? o.print("true") : o.print("false");
        comma(o, &c), o.print("\"type\":"), o.print(static_cast<int>(ic.type));
        if (ic.name[0] != '\0') comma(o, &c), o.print("\"name\":"), json_escape::writeQuoted(o, ic.name);
        if (ic.type == InputType::DIGITAL) {
            comma(o, &c), o.print("\"active_state\":"), ic.active_state ? o.print("true") : o.print("false");
        } else {
            comma(o, &c), o.print("\"pulse\":");
            o.print('{'), c = false;
            comma(o, &c), o.print("\"pulses_per_rev\":"), o.print(ic.pulse.pulses_per_rev);
            comma(o, &c), o.print("\"threshold_rpm\":"), o.print(ic.pulse.threshold_rpm);
            o.print('}');
        }
        o.print('}');
    }
    o.print(']');

    comma(o, &c), o.print("\"thermostat\":");
    o.print('{'), c = false;
    comma(o, &c), o.print("\"enabled\":"), cfg.thermostat.enabled ? o.print("true") : o.print("false");
    comma(o, &c), o.print("\"sensor_id\":"), o.print(cfg.thermostat.sensor_id);
    comma(o, &c), o.print("\"sensor_name\":"), json_escape::writeQuoted(o, findSensorNameById(cfg, cfg.thermostat.sensor_id));
    comma(o, &c), o.print("\"comparison\":"), json_escape::writeQuoted(o, cfg.thermostat.comparison);
    comma(o, &c), o.print("\"lower_threshold\":"), printJsonFloat(o, cfg.thermostat.lower_threshold);
    comma(o, &c), o.print("\"upper_threshold\":"), printJsonFloat(o, cfg.thermostat.upper_threshold);
    comma(o, &c), o.print("\"program_id_lower\":"), o.print(cfg.thermostat.program_id_lower);
    comma(o, &c), o.print("\"program_id_upper\":"), o.print(cfg.thermostat.program_id_upper);
    o.print('}');

    comma(o, &c), o.print("\"battery_saver\":");
    o.print('{'), c = false;
    comma(o, &c), o.print("\"enabled\":"), cfg.battery_saver.enabled ? o.print("true") : o.print("false");
    comma(o, &c), o.print("\"voltage_start_threshold\":"),
        printJsonFloat(o, cfg.battery_saver.voltage_start_threshold);
    comma(o, &c), o.print("\"voltage_abort_threshold\":"),
        printJsonFloat(o, cfg.battery_saver.voltage_abort_threshold);
    comma(o, &c), o.print("\"hysteresis\":"), printJsonFloat(o, cfg.battery_saver.hysteresis);
    comma(o, &c),
        o.print("\"min_low_voltage_duration_sec\":"), o.print(cfg.battery_saver.min_low_voltage_duration_sec);
    comma(o, &c),
        o.print("\"min_time_between_attempts_sec\":"), o.print(cfg.battery_saver.min_time_between_attempts_sec);
    comma(o, &c), o.print("\"max_attempts_per_day\":"), o.print(cfg.battery_saver.max_attempts_per_day);
    comma(o, &c), o.print("\"program_id\":"), o.print(cfg.battery_saver.program_id);
    o.print('}');

    comma(o, &c), o.print("\"time\":");
    o.print('{'), c = false;
    comma(o, &c), o.print("\"enabled\":"), cfg.time.enabled ? o.print("true") : o.print("false");
    comma(o, &c), o.print("\"ntp_server\":"), json_escape::writeQuoted(o, cfg.time.ntp_server);
    comma(o, &c), o.print("\"tz_offset_hours\":"), o.print(static_cast<int>(cfg.time.tz_offset_hours));
    comma(o, &c), o.print("\"sync_interval_hours\":"),
        o.print(static_cast<unsigned long>(cfg.time.sync_interval_hours));
    o.print('}');

    comma(o, &c), o.print("\"input_triggers\":[");
    for (uint8_t i = 0; i < cfg.input_triggers_count; i++) {
        const TriggerConfig& tc = cfg.input_triggers[i];
        if (i != 0) o.print(',');
        o.print('{'), c = false;
        if (tc.id) comma(o, &c), o.print("\"id\":"), o.print(tc.id);
        comma(o, &c), o.print("\"enabled\":"), tc.enabled ? o.print("true") : o.print("false");
        if (tc.input_id) comma(o, &c), o.print("\"input_id\":"), o.print(tc.input_id);
        comma(o, &c), o.print("\"trigger_level\":"), o.print(tc.trigger_level);
        comma(o, &c), o.print("\"program_id\":"), o.print(tc.program_id);
        o.print('}');
    }
    o.print(']');

    comma(o, &c), o.print("\"temperature_triggers\":[");
    for (uint8_t i = 0; i < cfg.temperature_triggers_count; i++) {
        const TempTriggerConfig& tt = cfg.temperature_triggers[i];
        if (i != 0) o.print(',');
        o.print('{'), c = false;
        if (tt.id) comma(o, &c), o.print("\"id\":"), o.print(tt.id);
        comma(o, &c), o.print("\"enabled\":"), tt.enabled ? o.print("true") : o.print("false");
        comma(o, &c), o.print("\"sensor_id\":"), o.print(tt.sensor_id);
        comma(o, &c), o.print("\"sensor_name\":"), json_escape::writeQuoted(o, findSensorNameById(cfg, tt.sensor_id));
        comma(o, &c), o.print("\"comparison\":"), json_escape::writeQuoted(o, tt.comparison);
        comma(o, &c), o.print("\"threshold\":"), printJsonFloat(o, tt.threshold);
        comma(o, &c), o.print("\"program_id\":"), o.print(tt.program_id);
        o.print('}');
    }
    o.print(']');

    comma(o, &c), o.print("\"schedule_triggers\":[");
    for (uint8_t i = 0; i < cfg.schedule_triggers_count; i++) {
        const ScheduleTriggerConfig& sc = cfg.schedule_triggers[i];
        if (i != 0) o.print(',');
        o.print('{'), c = false;
        if (sc.id) comma(o, &c), o.print("\"id\":"), o.print(sc.id);
        comma(o, &c), o.print("\"enabled\":"), sc.enabled ? o.print("true") : o.print("false");
        comma(o, &c), o.print("\"hour\":"), o.print(sc.hour);
        comma(o, &c), o.print("\"minute\":"), o.print(sc.minute);
        comma(o, &c), o.print("\"days_mask\":"), o.print(sc.days_mask);
        comma(o, &c), o.print("\"program_id\":"), o.print(sc.program_id);
        o.print('}');
    }
    o.print(']');

    o.print('}');
    return fc.n;
}

uint16_t crc16SerializedBaseConfig(const BaseConfig& cfg, size_t* outLen) {
    struct CrcSink : public Print {
        uint16_t crc;
        size_t len = 0;
        CrcSink() : crc(0xFFFF) {}
        size_t write(uint8_t b) override {
            crc = crc16ModbusNext(crc, b);
            len++;
            return 1;
        }
        size_t write(const uint8_t* buffer, size_t size) override {
            if (!buffer || size == 0) return 0;
            crc = crc16ModbusFeedBytes(crc, buffer, size);
            len += size;
            return size;
        }
        uint16_t getCrc() const { return crc; }
        size_t getLen() const { return len; }
    } sink;
    serializeBaseConfigToPrint(cfg, sink);
    if (outLen) *outLen = sink.getLen();
    return sink.getCrc();
}

} // namespace config_internal
