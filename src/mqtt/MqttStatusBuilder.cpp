#include "mqtt/MqttStatusBuilder.h"

#include "common/Pins.h"

#include <string.h>

namespace {

inline void comma(Print& p, bool* c) {
    if (*c) p.print(',');
    *c = true;
}

} // namespace

void emitMqttStatusJson(const StatusSnapshot& s, const BaseConfig& cfg, Print& p) {
    bool c = false;
    p.print('{');

    comma(p, &c);
    p.print("\"uptime\":");
    p.print(static_cast<unsigned long>(s.uptimeSec));

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
            snprintf(keybuf, sizeof keybuf, "\"%u\":{", (unsigned)s.tempSensors[i].id);
            p.print(keybuf);

            bool sc = false;
            comma(p, &sc);
            p.print("\"valid\":");
            p.print(s.tempSensors[i].valid ? "true" : "false");

            comma(p, &sc);
            p.print("\"lastMs\":");
            p.print(static_cast<unsigned long>(s.tempSensors[i].lastMs));

            comma(p, &sc);
            p.print("\"t\":");
            if (s.tempSensors[i].valid)
                p.print(s.tempSensors[i].t);
            else
                p.print("null");

            p.print('}');
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

    comma(p, &c);
    p.print("\"last_error\":\"");
    // NONE maps to "OK" in ErrorManager; MQTT contract uses empty string for no error.
    if (s.lastError && s.lastError[0] && strcmp(s.lastError, "OK") != 0) {
        for (const char* e = s.lastError; *e; e++) {
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
    p.print('"');

    p.print('}');
}
