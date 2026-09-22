// src/config/Config.BaseJsonEmit.cpp
#include "config/internal/BaseConfigJsonIo.h"

#include "common/Constants.h"
#include "common/Utils.h"
#include <stdlib.h>
#include <string.h>

namespace config_internal {

namespace {

void writeEscaped(Print& p, const char* s) {
    p.print('"');
    if (!s) {
        p.print('"');
        return;
    }
    for (const unsigned char* u = reinterpret_cast<const unsigned char*>(s); *u; u++) {
        const char c = static_cast<char>(*u);
        switch (c) {
            case '\"':
                p.print("\\\"");
                break;
            case '\\':
                p.print("\\\\");
                break;
            case '\b':
                p.print("\\b");
                break;
            case '\f':
                p.print("\\f");
                break;
            case '\n':
                p.print("\\n");
                break;
            case '\r':
                p.print("\\r");
                break;
            case '\t':
                p.print("\\t");
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20u) {
                    char buf[7];
                    snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                    p.print(buf);
                } else {
                    p.write(static_cast<unsigned char>(c));
                }
        }
    }
    p.print('"');
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
    comma(o, &c), o.print("\"ssid\":"), writeEscaped(o, cfg.wifi.ssid);
    comma(o, &c), o.print("\"password\":"), writeEscaped(o, cfg.wifi.password);
    comma(o, &c), o.print("\"ap_timeout_sec\":"), o.print(cfg.wifi.ap_timeout_sec);
    comma(o, &c), o.print("\"ap_timeout_enabled\":"), cfg.wifi.ap_timeout_enabled ? o.print("true") : o.print("false");
    o.print('}');

    comma(o, &c), o.print("\"gsm\":");
    o.print('{'), c = false;
    comma(o, &c), o.print("\"apn\":"), writeEscaped(o, cfg.gsm.apn);
    comma(o, &c), o.print("\"apn_user\":"), writeEscaped(o, cfg.gsm.apn_user);
    comma(o, &c), o.print("\"apn_pass\":"), writeEscaped(o, cfg.gsm.apn_pass);
    comma(o, &c), o.print("\"owner_phone\":"), writeEscaped(o, cfg.gsm.owner_phone);
    comma(o, &c), o.print("\"dtmf_password\":"), writeEscaped(o, cfg.gsm.dtmf_password);
    o.print('}');

    comma(o, &c), o.print("\"mqtt\":");
    o.print('{'), c = false;
    comma(o, &c), o.print("\"broker\":"), writeEscaped(o, cfg.mqtt.broker);
    comma(o, &c), o.print("\"port\":"), o.print(cfg.mqtt.port);
    comma(o, &c), o.print("\"client_id\":"), writeEscaped(o, cfg.mqtt.client_id);
    comma(o, &c), o.print("\"user\":"), writeEscaped(o, cfg.mqtt.user);
    comma(o, &c), o.print("\"pass\":"), writeEscaped(o, cfg.mqtt.pass);
    comma(o, &c), o.print("\"topic_prefix\":"), writeEscaped(o, cfg.mqtt.topic_prefix);
    comma(o, &c), o.print("\"publish_interval_sec\":"), o.print(cfg.mqtt.publish_interval_sec);
    o.print('}');

    comma(o, &c), o.print("\"vehicle\":");
    o.print('{'), c = false;
    comma(o, &c), o.print("\"adc_voltage_coeff\":"), o.print(cfg.vehicle.adc_voltage_coeff);
    comma(o, &c), o.print("\"starter_max_time_sec\":"), o.print(cfg.vehicle.starter_max_time_sec);
    comma(o, &c), o.print("\"wait_after_start_sec\":"), o.print(cfg.vehicle.wait_after_start_sec);
    comma(o, &c), o.print("\"engine_running_voltage_threshold\":"),
        o.print(cfg.vehicle.engine_running_voltage_threshold);
    comma(o, &c), o.print("\"engine_stopped_voltage_threshold\":"),
        o.print(cfg.vehicle.engine_stopped_voltage_threshold);
    comma(o, &c), o.print("\"starter_relay_id\":"), o.print(cfg.vehicle.starter_relay_id);
    comma(o, &c), o.print("\"engine_detection_source\":"), writeEscaped(o, cfg.vehicle.engine_detection_source);
    comma(o, &c), o.print("\"engine_input_id\":"), o.print(cfg.vehicle.engine_input_id);
    o.print('}');

    comma(o, &c), o.print("\"sensors\":[");
    for (uint8_t i = 0; i < HardwareLimits::SENSORS; i++) {
        if (i != 0) o.print(',');
        o.print('{'), c = false;
        const SensorConfig& sc = cfg.sensors[i];
        if (sc.id != 0) comma(o, &c), o.print("\"id\":"), o.print(sc.id);
        comma(o, &c), o.print("\"coeff\":"), o.print(sc.coeff);
        if (sc.rom[0] != '\0') comma(o, &c), o.print("\"rom\":"), writeEscaped(o, sc.rom);
        if (sc.name[0] != '\0') comma(o, &c), o.print("\"name\":"), writeEscaped(o, sc.name);
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
        if (ic.name[0] != '\0') comma(o, &c), o.print("\"name\":"), writeEscaped(o, ic.name);
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
    comma(o, &c), o.print("\"sensor_name\":"), writeEscaped(o, findSensorNameById(cfg, cfg.thermostat.sensor_id));
    comma(o, &c), o.print("\"comparison\":"), writeEscaped(o, cfg.thermostat.comparison);
    comma(o, &c), o.print("\"lower_threshold\":"), o.print(cfg.thermostat.lower_threshold);
    comma(o, &c), o.print("\"upper_threshold\":"), o.print(cfg.thermostat.upper_threshold);
    comma(o, &c), o.print("\"program_id_lower\":"), o.print(cfg.thermostat.program_id_lower);
    comma(o, &c), o.print("\"program_id_upper\":"), o.print(cfg.thermostat.program_id_upper);
    o.print('}');

    comma(o, &c), o.print("\"battery_saver\":");
    o.print('{'), c = false;
    comma(o, &c), o.print("\"enabled\":"), cfg.battery_saver.enabled ? o.print("true") : o.print("false");
    comma(o, &c), o.print("\"voltage_start_threshold\":"),
        o.print(cfg.battery_saver.voltage_start_threshold);
    comma(o, &c), o.print("\"voltage_abort_threshold\":"),
        o.print(cfg.battery_saver.voltage_abort_threshold);
    comma(o, &c), o.print("\"hysteresis\":"), o.print(cfg.battery_saver.hysteresis);
    comma(o, &c),
        o.print("\"min_low_voltage_duration_sec\":"), o.print(cfg.battery_saver.min_low_voltage_duration_sec);
    comma(o, &c),
        o.print("\"min_time_between_attempts_sec\":"), o.print(cfg.battery_saver.min_time_between_attempts_sec);
    comma(o, &c), o.print("\"max_attempts_per_day\":"), o.print(cfg.battery_saver.max_attempts_per_day);
    comma(o, &c), o.print("\"program_id\":"), o.print(cfg.battery_saver.program_id);
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
        comma(o, &c), o.print("\"sensor_name\":"), writeEscaped(o, findSensorNameById(cfg, tt.sensor_id));
        comma(o, &c), o.print("\"comparison\":"), writeEscaped(o, tt.comparison);
        comma(o, &c), o.print("\"threshold\":"), o.print(tt.threshold);
        comma(o, &c), o.print("\"program_id\":"), o.print(tt.program_id);
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
