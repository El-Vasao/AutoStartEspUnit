#include "mqtt/MqttStatusBuilder.h"

#include "common/Constants.h"
#include "common/Pins.h"

#include <math.h>
#include <string.h>

namespace {

inline void comma(Print& p, bool* c) {
    if (*c) p.print(',');
    *c = true;
}

inline bool floatChanged(float a, float b, float eps) {
    return fabsf(a - b) >= eps;
}

inline bool lastErrChanged(const StatusSnapshot& cur, const StatusSnapshot& prev) {
    if (cur.lastErrCount != prev.lastErrCount) return true;
    for (uint8_t i = 0; i < cur.lastErrCount && i < ErrorHistory::CAPACITY; i++) {
        if (cur.lastErr[i].code != prev.lastErr[i].code) return true;
        if (cur.lastErr[i].active != prev.lastErr[i].active) return true;
        if (cur.lastErr[i].uptimeSec != prev.lastErr[i].uptimeSec) return true;
    }
    return false;
}

inline void escapeMsg(Print& p, const char* s) {
    if (!s) return;
    for (const char* e = s; *e; e++) {
        switch (*e) {
            case '\\':
                p.print("\\\\");
                break;
            case '"':
                p.print("\\\"");
                break;
            case '\n':
                p.print("\\n");
                break;
            case '\r':
                p.print("\\r");
                break;
            default:
                p.write(static_cast<uint8_t>(*e));
                break;
        }
    }
}

/// Emit `last_err` array only when count > 0 (omit key when empty).
inline void printLastErrArray(Print& p, const StatusSnapshot& s) {
    if (s.lastErrCount == 0) return;
    p.print("\"last_err\":[");
    for (uint8_t i = 0; i < s.lastErrCount && i < ErrorHistory::CAPACITY; i++) {
        if (i) p.print(',');
        p.print("{\"code\":");
        p.print((unsigned)s.lastErr[i].code);
        p.print(",\"msg\":\"");
        escapeMsg(p, s.lastErr[i].msg);
        p.print("\",\"active\":");
        p.print(s.lastErr[i].active ? "true" : "false");
        p.print('}');
    }
    p.print(']');
}

inline void printTempSensorObject(Print& p, const StatusSnapshot::TempSensor& ts) {
    p.print('{');
    bool sc = false;
    comma(p, &sc);
    p.print("\"valid\":");
    p.print(ts.valid ? "true" : "false");
    comma(p, &sc);
    p.print("\"lastMs\":");
    p.print(static_cast<unsigned long>(ts.lastMs));
    comma(p, &sc);
    p.print("\"t\":");
    if (ts.valid)
        p.print(ts.t);
    else
        p.print("null");
    p.print('}');
}

bool voltageChanged(const StatusSnapshot& cur, const StatusSnapshot& prev) {
    if (cur.voltageValid != prev.voltageValid) return true;
    if (!cur.voltageValid) return false;
    return floatChanged(cur.voltage, prev.voltage, JsonBytes::Mqtt::STATUS_VOLTAGE_EPS);
}

bool tempSensorChanged(const StatusSnapshot::TempSensor& cur, const StatusSnapshot::TempSensor& prev) {
    if (cur.id != prev.id) return true;
    if (!cur.id) return false;
    if (cur.valid != prev.valid) return true;
    if (!cur.valid) return false;
    return floatChanged(cur.t, prev.t, JsonBytes::Mqtt::STATUS_TEMP_EPS);
}

bool anyTempChanged(const StatusSnapshot& cur, const StatusSnapshot& prev) {
    for (int i = 0; i < HardwareLimits::SENSORS; i++) {
        if (tempSensorChanged(cur.tempSensors[i], prev.tempSensors[i])) return true;
    }
    return false;
}

bool anyInputChanged(const StatusSnapshot& cur, const StatusSnapshot& prev) {
    for (int i = 0; i < HardwareLimits::INPUTS; i++) {
        if (cur.inputState[i] != prev.inputState[i]) return true;
    }
    return false;
}

bool anyRelayChanged(const StatusSnapshot& cur, const StatusSnapshot& prev) {
    for (int i = 0; i < HardwareLimits::RELAYS; i++) {
        if (cur.relayState[i] != prev.relayState[i]) return true;
    }
    return false;
}

bool anyTriggerChanged(const StatusSnapshot& cur, const StatusSnapshot& prev, const BaseConfig& cfg) {
    for (uint8_t i = 0; i < cfg.input_triggers_count && i < Limits::MAX_TRIGGERS; i++) {
        if (!cfg.input_triggers[i].id) continue;
        if (cur.inputTriggerRuntime[i] != prev.inputTriggerRuntime[i]) return true;
    }
    for (uint8_t i = 0; i < cfg.temperature_triggers_count && i < Limits::MAX_TRIGGERS; i++) {
        if (!cfg.temperature_triggers[i].id) continue;
        if (cur.tempTriggerRuntime[i] != prev.tempTriggerRuntime[i]) return true;
    }
    return false;
}

bool programFieldsChanged(const StatusSnapshot& cur, const StatusSnapshot& prev) {
    if (cur.programRunning != prev.programRunning) return true;
    if (cur.currentProgramId != prev.currentProgramId) return true;
    if (cur.lastProgramId != prev.lastProgramId) return true;
    return false;
}

} // namespace

bool mqttStatusHasSignificantChanges(const StatusSnapshot& cur, const StatusSnapshot& prev,
                                     const BaseConfig& cfg) {
    if (strcmp(cur.modeName, prev.modeName) != 0) return true;
    if (voltageChanged(cur, prev)) return true;
    if (cur.engineRunning != prev.engineRunning) return true;
    if (anyInputChanged(cur, prev)) return true;
    if (anyRelayChanged(cur, prev)) return true;
    if (anyTempChanged(cur, prev)) return true;
    if (programFieldsChanged(cur, prev)) return true;
    if (anyTriggerChanged(cur, prev, cfg)) return true;
    if (lastErrChanged(cur, prev)) return true;
    if (cur.lastErrCount > 0) return true;
    if (cur.timeSynced != prev.timeSynced) return true;
    if (cur.tzOffsetHours != prev.tzOffsetHours) return true;
    if (strcmp(cur.timeSource, prev.timeSource) != 0) return true;
    // Epoch ticks every second when synced — do not treat as significant (avoid MQTT spam).
    return false;
}

void emitMqttStatusJson(const StatusSnapshot& s, const BaseConfig& cfg, Print& p) {
    bool c = false;
    p.print('{');

    comma(p, &c);
    p.print("\"full\":true");

    comma(p, &c);
    p.print("\"uptime\":");
    p.print(static_cast<unsigned long>(s.uptimeSec));

    comma(p, &c);
    p.print("\"epoch\":");
    p.print(static_cast<unsigned long>(s.epochUtc));
    comma(p, &c);
    p.print("\"synced\":");
    p.print(s.timeSynced ? "true" : "false");
    comma(p, &c);
    p.print("\"tzOffsetHours\":");
    p.print(static_cast<long>(s.tzOffsetHours));
    comma(p, &c);
    p.print("\"timeSource\":\"");
    p.print(s.timeSource);
    p.print('"');

    comma(p, &c);
    p.print("\"mode\":\"");
    p.print(s.modeName ? s.modeName : "");
    p.print('"');

    comma(p, &c);
    p.print("\"voltage\":");
    if (s.voltageValid) {
        p.print(s.voltage);
    } else {
        p.print("null");
    }

    comma(p, &c);
    p.print("\"engineRunning\":");
    p.print(s.engineRunning ? "true" : "false");

    comma(p, &c);
    p.print("\"inputsById\":{");
    {
        bool ik = false;
        for (int i = 0; i < HardwareLimits::INPUTS; i++) {
            comma(p, &ik);
            char kb[16];
            snprintf(kb, sizeof kb, "\"%u\":", (unsigned)Pin::INPUT_IDS[i]);
            p.print(kb);
            p.print(s.inputState[i] ? "true" : "false");
        }
    }
    p.print('}');

    comma(p, &c);
    p.print("\"relaysById\":{");
    {
        bool rk = false;
        for (int i = 0; i < HardwareLimits::RELAYS; i++) {
            comma(p, &rk);
            char keybuf[16];
            snprintf(keybuf, sizeof keybuf, "\"%u\":", (unsigned)Pin::RELAY_IDS[i]);
            p.print(keybuf);
            p.print(s.relayState[i] ? "true" : "false");
        }
    }
    p.print('}');

    comma(p, &c);
    p.print("\"tempSensorsById\":{");
    {
        bool sk = false;
        for (int i = 0; i < HardwareLimits::SENSORS; i++) {
            if (!s.tempSensors[i].id) continue;
            comma(p, &sk);
            char keybuf[16];
            snprintf(keybuf, sizeof keybuf, "\"%u\":", (unsigned)s.tempSensors[i].id);
            p.print(keybuf);
            printTempSensorObject(p, s.tempSensors[i]);
        }
    }
    p.print('}');

    if (s.programRunning) {
        comma(p, &c);
        p.print("\"current_program\":");
        p.print(s.currentProgramId);
    }

    comma(p, &c);
    p.print("\"last_program\":");
    p.print(s.lastProgramId);

    comma(p, &c);
    p.print("\"runtime\":{");

    bool rr = false;
    comma(p, &rr);
    p.print("\"inputTriggersById\":{");
    {
        bool ik = false;
        for (uint8_t i = 0; i < cfg.input_triggers_count; i++) {
            const uint16_t id = cfg.input_triggers[i].id;
            if (!id) continue;
            comma(p, &ik);
            char keybuf[16];
            snprintf(keybuf, sizeof keybuf, "\"%u\":", (unsigned)id);
            p.print(keybuf);
            p.print((i < Limits::MAX_TRIGGERS) && s.inputTriggerRuntime[i] ? "true" : "false");
        }
    }
    p.print('}');

    comma(p, &rr);
    p.print("\"tempTriggersById\":{");
    {
        bool tk = false;
        for (uint8_t i = 0; i < cfg.temperature_triggers_count; i++) {
            const uint16_t id = cfg.temperature_triggers[i].id;
            if (!id) continue;
            comma(p, &tk);
            char keybuf[16];
            snprintf(keybuf, sizeof keybuf, "\"%u\":", (unsigned)id);
            p.print(keybuf);
            p.print((i < Limits::MAX_TRIGGERS) && s.tempTriggerRuntime[i] ? "true" : "false");
        }
    }
    p.print('}');

    p.print('}');

    if (s.lastErrCount > 0) {
        comma(p, &c);
        printLastErrArray(p, s);
    }

    p.print('}');
}

void emitMqttStatusDeltaJson(const StatusSnapshot& cur, const StatusSnapshot& prev, const BaseConfig& cfg,
                             Print& p) {
    bool c = false;
    p.print('{');

    comma(p, &c);
    p.print("\"full\":false");

    // Always include uptime when emitting a delta (liveness); caller ensures significant changes exist.
    comma(p, &c);
    p.print("\"uptime\":");
    p.print(static_cast<unsigned long>(cur.uptimeSec));

    if (cur.epochUtc != prev.epochUtc || cur.timeSynced != prev.timeSynced ||
        cur.tzOffsetHours != prev.tzOffsetHours || strcmp(cur.timeSource, prev.timeSource) != 0) {
        comma(p, &c);
        p.print("\"epoch\":");
        p.print(static_cast<unsigned long>(cur.epochUtc));
        comma(p, &c);
        p.print("\"synced\":");
        p.print(cur.timeSynced ? "true" : "false");
        comma(p, &c);
        p.print("\"tzOffsetHours\":");
        p.print(static_cast<long>(cur.tzOffsetHours));
        comma(p, &c);
        p.print("\"timeSource\":\"");
        p.print(cur.timeSource);
        p.print('"');
    }

    if (strcmp(cur.modeName, prev.modeName) != 0) {
        comma(p, &c);
        p.print("\"mode\":\"");
        p.print(cur.modeName ? cur.modeName : "");
        p.print('"');
    }

    if (voltageChanged(cur, prev)) {
        comma(p, &c);
        p.print("\"voltage\":");
        if (cur.voltageValid) {
            p.print(cur.voltage);
        } else {
            p.print("null");
        }
    }

    if (cur.engineRunning != prev.engineRunning) {
        comma(p, &c);
        p.print("\"engineRunning\":");
        p.print(cur.engineRunning ? "true" : "false");
    }

    if (anyInputChanged(cur, prev)) {
        comma(p, &c);
        p.print("\"inputsById\":{");
        bool ik = false;
        for (int i = 0; i < HardwareLimits::INPUTS; i++) {
            if (cur.inputState[i] == prev.inputState[i]) continue;
            comma(p, &ik);
            char kb[16];
            snprintf(kb, sizeof kb, "\"%u\":", (unsigned)Pin::INPUT_IDS[i]);
            p.print(kb);
            p.print(cur.inputState[i] ? "true" : "false");
        }
        p.print('}');
    }

    if (anyRelayChanged(cur, prev)) {
        comma(p, &c);
        p.print("\"relaysById\":{");
        bool rk = false;
        for (int i = 0; i < HardwareLimits::RELAYS; i++) {
            if (cur.relayState[i] == prev.relayState[i]) continue;
            comma(p, &rk);
            char keybuf[16];
            snprintf(keybuf, sizeof keybuf, "\"%u\":", (unsigned)Pin::RELAY_IDS[i]);
            p.print(keybuf);
            p.print(cur.relayState[i] ? "true" : "false");
        }
        p.print('}');
    }

    if (anyTempChanged(cur, prev)) {
        comma(p, &c);
        p.print("\"tempSensorsById\":{");
        bool sk = false;
        for (int i = 0; i < HardwareLimits::SENSORS; i++) {
            if (!tempSensorChanged(cur.tempSensors[i], prev.tempSensors[i])) continue;
            // Id became 0: omit from delta (consumer keeps last known; rare remapping).
            if (!cur.tempSensors[i].id) continue;
            comma(p, &sk);
            char keybuf[16];
            snprintf(keybuf, sizeof keybuf, "\"%u\":", (unsigned)cur.tempSensors[i].id);
            p.print(keybuf);
            printTempSensorObject(p, cur.tempSensors[i]);
        }
        p.print('}');
    }

    if (programFieldsChanged(cur, prev)) {
        if (cur.programRunning) {
            comma(p, &c);
            p.print("\"current_program\":");
            p.print(cur.currentProgramId);
        } else if (prev.programRunning) {
            comma(p, &c);
            p.print("\"current_program\":null");
        }
        if (cur.lastProgramId != prev.lastProgramId) {
            comma(p, &c);
            p.print("\"last_program\":");
            p.print(cur.lastProgramId);
        }
    }

    if (anyTriggerChanged(cur, prev, cfg)) {
        comma(p, &c);
        p.print("\"runtime\":{");
        bool rr = false;

        bool anyIn = false;
        for (uint8_t i = 0; i < cfg.input_triggers_count && i < Limits::MAX_TRIGGERS; i++) {
            if (!cfg.input_triggers[i].id) continue;
            if (cur.inputTriggerRuntime[i] != prev.inputTriggerRuntime[i]) {
                anyIn = true;
                break;
            }
        }
        if (anyIn) {
            comma(p, &rr);
            p.print("\"inputTriggersById\":{");
            bool ik = false;
            for (uint8_t i = 0; i < cfg.input_triggers_count && i < Limits::MAX_TRIGGERS; i++) {
                const uint16_t id = cfg.input_triggers[i].id;
                if (!id) continue;
                if (cur.inputTriggerRuntime[i] == prev.inputTriggerRuntime[i]) continue;
                comma(p, &ik);
                char keybuf[16];
                snprintf(keybuf, sizeof keybuf, "\"%u\":", (unsigned)id);
                p.print(keybuf);
                p.print(cur.inputTriggerRuntime[i] ? "true" : "false");
            }
            p.print('}');
        }

        bool anyTempTrig = false;
        for (uint8_t i = 0; i < cfg.temperature_triggers_count && i < Limits::MAX_TRIGGERS; i++) {
            if (!cfg.temperature_triggers[i].id) continue;
            if (cur.tempTriggerRuntime[i] != prev.tempTriggerRuntime[i]) {
                anyTempTrig = true;
                break;
            }
        }
        if (anyTempTrig) {
            comma(p, &rr);
            p.print("\"tempTriggersById\":{");
            bool tk = false;
            for (uint8_t i = 0; i < cfg.temperature_triggers_count && i < Limits::MAX_TRIGGERS; i++) {
                const uint16_t id = cfg.temperature_triggers[i].id;
                if (!id) continue;
                if (cur.tempTriggerRuntime[i] == prev.tempTriggerRuntime[i]) continue;
                comma(p, &tk);
                char keybuf[16];
                snprintf(keybuf, sizeof keybuf, "\"%u\":", (unsigned)id);
                p.print(keybuf);
                p.print(cur.tempTriggerRuntime[i] ? "true" : "false");
            }
            p.print('}');
        }

        p.print('}');
    }

    if (cur.lastErrCount > 0) {
        comma(p, &c);
        printLastErrArray(p, cur);
    }

    p.print('}');
}
