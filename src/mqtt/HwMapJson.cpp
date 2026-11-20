#include "mqtt/HwMapJson.h"

#include "common/ChannelSilk.h"
#include "common/Fnv1a.h"
#include "common/JsonEscape.h"
#include "common/Pins.h"

namespace {

void comma(Print& p, bool* started) {
    if (*started) p.print(',');
    *started = true;
}

void emitRelays(Print& p, const BaseConfig& cfg) {
    p.print('[');
    bool any = false;
    for (uint8_t i = 0; i < HardwareLimits::RELAYS; i++) {
        const uint8_t n = ChannelSilk::numberFromSlot(i, ChannelSilk::Kind::Relay);
        if (n == ChannelSilk::NONE) continue;
        if (any) p.print(',');
        any = true;
        p.print("{\"id\":");
        ChannelSilk::printQuotedId(p, n, ChannelSilk::Kind::Relay);
        p.print(",\"name\":");
        json_escape::writeQuoted(p, cfg.relays[i].name);
        p.print('}');
    }
    p.print(']');
}

void emitInputs(Print& p, const BaseConfig& cfg) {
    p.print('[');
    bool any = false;
    for (uint8_t i = 0; i < HardwareLimits::INPUTS; i++) {
        if (cfg.inputs[i].id == 0) continue;
        if (any) p.print(',');
        any = true;
        p.print("{\"id\":");
        ChannelSilk::printQuotedId(p, cfg.inputs[i].id, ChannelSilk::Kind::Input);
        p.print(",\"name\":");
        json_escape::writeQuoted(p, cfg.inputs[i].name);
        p.print('}');
    }
    p.print(']');
}

void emitSensors(Print& p, const BaseConfig& cfg) {
    p.print('[');
    bool any = false;
    for (uint8_t i = 0; i < HardwareLimits::SENSORS; i++) {
        if (cfg.sensors[i].id == 0) continue;
        if (any) p.print(',');
        any = true;
        p.print("{\"id\":");
        ChannelSilk::printQuotedId(p, cfg.sensors[i].id, ChannelSilk::Kind::Sensor);
        p.print(",\"name\":");
        json_escape::writeQuoted(p, cfg.sensors[i].name);
        p.print('}');
    }
    p.print(']');
}

void emitInputTriggers(Print& p, const BaseConfig& cfg) {
    p.print('[');
    bool any = false;
    const uint8_t n = cfg.input_triggers_count < Limits::MAX_TRIGGERS ? cfg.input_triggers_count
                                                                      : Limits::MAX_TRIGGERS;
    for (uint8_t i = 0; i < n; i++) {
        const TriggerConfig& t = cfg.input_triggers[i];
        if (t.id == 0) continue;
        if (any) p.print(',');
        any = true;
        p.print("{\"id\":");
        p.print(static_cast<unsigned>(t.id));
        p.print(",\"inputId\":");
        ChannelSilk::printQuotedId(p, t.input_id, ChannelSilk::Kind::Input);
        p.print(",\"programId\":");
        p.print(static_cast<unsigned>(t.program_id));
        p.print(",\"level\":");
        p.print(static_cast<unsigned>(t.trigger_level));
        p.print('}');
    }
    p.print(']');
}

void emitTempTriggers(Print& p, const BaseConfig& cfg) {
    p.print('[');
    bool any = false;
    const uint8_t n = cfg.temperature_triggers_count < Limits::MAX_TRIGGERS ? cfg.temperature_triggers_count
                                                                            : Limits::MAX_TRIGGERS;
    for (uint8_t i = 0; i < n; i++) {
        const TempTriggerConfig& t = cfg.temperature_triggers[i];
        if (t.id == 0) continue;
        if (any) p.print(',');
        any = true;
        p.print("{\"id\":");
        p.print(static_cast<unsigned>(t.id));
        p.print(",\"sensorId\":");
        ChannelSilk::printQuotedId(p, t.sensor_id, ChannelSilk::Kind::Sensor);
        p.print(",\"programId\":");
        p.print(static_cast<unsigned>(t.program_id));
        p.print(",\"comparison\":");
        json_escape::writeQuoted(p, t.comparison);
        p.print(",\"threshold\":");
        p.print(static_cast<double>(t.threshold), 2);
        p.print('}');
    }
    p.print(']');
}

} // namespace

void emitHwMapObject(const BaseConfig& cfg, Print& p) {
    p.print('{');
    bool c = false;
    comma(p, &c);
    p.print("\"relays\":");
    emitRelays(p, cfg);
    comma(p, &c);
    p.print("\"inputs\":");
    emitInputs(p, cfg);
    comma(p, &c);
    p.print("\"sensors\":");
    emitSensors(p, cfg);
    p.print('}');
}

void emitTrigMapObject(const BaseConfig& cfg, Print& p) {
    p.print('{');
    bool c = false;
    comma(p, &c);
    p.print("\"inputTriggers\":");
    emitInputTriggers(p, cfg);
    comma(p, &c);
    p.print("\"tempTriggers\":");
    emitTempTriggers(p, cfg);
    p.print('}');
}

uint32_t hwMapObjectFnv(const BaseConfig& cfg) {
    Fnv1aPrint fp;
    emitHwMapObject(cfg, fp);
    return fp.hash();
}

uint32_t trigMapObjectFnv(const BaseConfig& cfg) {
    Fnv1aPrint fp;
    emitTrigMapObject(cfg, fp);
    return fp.hash();
}
