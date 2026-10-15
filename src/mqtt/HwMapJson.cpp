#include "mqtt/HwMapJson.h"

#include "common/Fnv1a.h"
#include "common/JsonEscape.h"
#include "common/Pins.h"

namespace {

void comma(Print& p, bool* started) {
    if (*started) p.print(',');
    *started = true;
}

void emitRelays(Print& p, const BaseConfig&) {
    p.print('[');
    bool any = false;
    for (uint8_t i = 0; i < HardwareLimits::RELAYS; i++) {
        if (Pin::RELAY_IDS[i] == 0) continue;
        if (any) p.print(',');
        any = true;
        p.print("{\"id\":");
        p.print(static_cast<unsigned>(Pin::RELAY_IDS[i]));
        p.print(",\"name\":\"\"}");
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
        p.print(static_cast<unsigned>(cfg.inputs[i].id));
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
        p.print(static_cast<unsigned>(cfg.sensors[i].id));
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
        p.print(static_cast<unsigned>(t.input_id));
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
        p.print(static_cast<unsigned>(t.sensor_id));
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

void emitScheduleTriggers(Print& p, const BaseConfig& cfg) {
    p.print('[');
    bool any = false;
    const uint8_t n = cfg.schedule_triggers_count < Limits::MAX_SCHEDULE_TRIGGERS
                          ? cfg.schedule_triggers_count
                          : Limits::MAX_SCHEDULE_TRIGGERS;
    for (uint8_t i = 0; i < n; i++) {
        const ScheduleTriggerConfig& t = cfg.schedule_triggers[i];
        if (t.id == 0) continue;
        if (any) p.print(',');
        any = true;
        p.print("{\"id\":");
        p.print(static_cast<unsigned>(t.id));
        p.print(",\"programId\":");
        p.print(static_cast<unsigned>(t.program_id));
        p.print(",\"hour\":");
        p.print(static_cast<unsigned>(t.hour));
        p.print(",\"minute\":");
        p.print(static_cast<unsigned>(t.minute));
        p.print(",\"days\":");
        p.print(static_cast<unsigned>(t.days_mask));
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
    comma(p, &c);
    p.print("\"inputTriggers\":");
    emitInputTriggers(p, cfg);
    comma(p, &c);
    p.print("\"tempTriggers\":");
    emitTempTriggers(p, cfg);
    comma(p, &c);
    p.print("\"scheduleTriggers\":");
    emitScheduleTriggers(p, cfg);
    p.print('}');
}

uint32_t hwMapObjectFnv(const BaseConfig& cfg) {
    Fnv1aPrint fp;
    emitHwMapObject(cfg, fp);
    return fp.hash();
}
