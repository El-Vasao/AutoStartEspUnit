/**
 * @file WebServer.SseBuilders.cpp
 * @brief Domain SSE/bootstrap JSON builders (hardware / runtime / program / flash / clocks / snapshot).
 */
#include "web/internal/SseIncCommon.h"

#include "config/Config.h"
#include "config/SensorRomResolve.h"
#include "common/Pins.h"
#include "common/Utils.h"
#include "core/ErrorManager.h"
#include "core/FlashCommitCoordinator.h"
#include "gsm/GSMController.h"
#include "io/DigitalInputs.h"
#include "io/RelayController.h"
#include "io/SensorsController.h"
#include "program/ProgramExecutor.h"

namespace sse_inc_detail {

char gSsePayload[kPayloadCap];

void emitRelayInputTempVoltageMaps(Print& p, bool* needComma, const SseStatusPort& st) {
    if (!st.relay || !st.inputs || !st.sensors) return;

    commaOut(p, needComma);
    p.print("\"relaysById\":{");
    {
        bool rk = false;
        for (int i = 0; i < HardwareLimits::RELAYS; i++) {
            commaOut(p, &rk);
            char kb[16];
            snprintf(kb, sizeof kb, "\"%u\":", (unsigned)Pin::RELAY_IDS[i]);
            p.print(kb);
            p.print(st.relay->getState(i) ? "true" : "false");
        }
    }
    p.print('}');

    commaOut(p, needComma);
    p.print("\"inputsById\":{");
    {
        bool fk = false;
        for (int i = 0; i < HardwareLimits::INPUTS; i++) {
            commaOut(p, &fk);
            char kb[16];
            snprintf(kb, sizeof kb, "\"%u\":", (unsigned)Pin::INPUT_IDS[i]);
            p.print(kb);
            p.print(st.inputs->getState(i) ? "true" : "false");
        }
    }
    p.print('}');

    commaOut(p, needComma);
    p.print("\"inputFrequenciesById\":{");
    {
        bool fk = false;
        for (int i = 0; i < HardwareLimits::INPUTS; i++) {
            commaOut(p, &fk);
            char kb[16];
            snprintf(kb, sizeof kb, "\"%u\":", (unsigned)Pin::INPUT_IDS[i]);
            p.print(kb);
            if (st.inputs->isCounter(i)) {
                p.print(st.inputs->getFrequency(i));
            } else {
                p.print("null");
            }
        }
    }
    p.print('}');

    commaOut(p, needComma);
    p.print("\"inputsEnabledById\":{");
    {
        bool fk = false;
        for (int i = 0; i < HardwareLimits::INPUTS; i++) {
            commaOut(p, &fk);
            char kb[16];
            snprintf(kb, sizeof kb, "\"%u\":", (unsigned)Pin::INPUT_IDS[i]);
            p.print(kb);
            p.print(st.inputs->isRuntimeEnabled(i) ? "true" : "false");
        }
    }
    p.print('}');

    commaOut(p, needComma);
    p.print("\"tempSensors\":[");
    for (int i = 0; i < HardwareLimits::SENSORS; i++) {
        if (i != 0) p.print(',');
        p.print('{');
        bool tc = false;

        uint16_t sid = resolveSensorConfigId(st.sensors->getSensorAddress(i), config.getBase());

        commaOut(p, &tc);
        p.print("\"id\":");
        if (sid)
            p.print(sid);
        else
            p.print("null");

        const bool valid = st.sensors->isTemperatureValid(i);
        commaOut(p, &tc);
        p.print("\"valid\":");
        p.print(valid ? "true" : "false");

        commaOut(p, &tc);
        p.print("\"lastMs\":");
        p.print(static_cast<unsigned long>(st.sensors->getLastTemperatureTime(i)));

        commaOut(p, &tc);
        p.print("\"t\":");
        if (valid)
            p.print(st.sensors->getTemperature(i));
        else
            p.print("null");

        p.print('}');
    }
    p.print(']');

    commaOut(p, needComma);
    p.print("\"voltage\":");
    if (st.sensors->isVoltageValid())
        p.print(st.sensors->getVoltage());
    else
        p.print("null");
}

void emitHardwarePayload(Print& p, const SseStatusPort& st) {
    bool c = false;
    p.print('{');
    emitRelayInputTempVoltageMaps(p, &c, st);
    commaOut(p, &c);
    p.print("\"engineRunning\":");
    p.print(st.engineRunning ? "true" : "false");
    p.print('}');
}

static void emitRuntimeObject(Print& p, const SseStatusPort& st) {
    p.print('{');
    bool rr = false;
    commaOut(p, &rr);
    p.print("\"thermostat\":");
    p.print(st.thermostatRuntime ? "true" : "false");
    commaOut(p, &rr);
    p.print("\"batterySaver\":");
    p.print(st.batterySaverRuntime ? "true" : "false");
    commaOut(p, &rr);
    p.print("\"inputTriggersById\":{");
    {
        bool ik = false;
        for (uint8_t i = 0; i < config.getBase().input_triggers_count; i++) {
            const uint16_t id = config.getBase().input_triggers[i].id;
            if (!id) continue;
            commaOut(p, &ik);
            char kb[16];
            snprintf(kb, sizeof kb, "\"%u\":", (unsigned)id);
            p.print(kb);
            const bool en = st.getTriggerRuntime && st.getTriggerRuntime(st.triggerCtx, i);
            p.print(en ? "true" : "false");
        }
    }
    p.print('}');
    commaOut(p, &rr);
    p.print("\"tempTriggersById\":{");
    {
        bool tk = false;
        for (uint8_t i = 0; i < config.getBase().temperature_triggers_count; i++) {
            const uint16_t id = config.getBase().temperature_triggers[i].id;
            if (!id) continue;
            commaOut(p, &tk);
            char kb[16];
            snprintf(kb, sizeof kb, "\"%u\":", (unsigned)id);
            p.print(kb);
            const bool en = st.getTempTriggerRuntime && st.getTempTriggerRuntime(st.triggerCtx, i);
            p.print(en ? "true" : "false");
        }
    }
    p.print('}');
    p.print('}');
}

void emitRuntimePayload(Print& p, const SseStatusPort& st) {
    p.print('{');
    p.print("\"runtime\":");
    emitRuntimeObject(p, st);
    p.print('}');
}

static void emitProgramFields(Print& p, bool* needComma, const SseStatusPort& st) {
    if (!st.program) return;
    commaOut(p, needComma);
    p.print("\"lastProgramName\":\"");
    {
        const char* lastName = st.program->getLastProgramName();
        escapeJsonString(p, (lastName && lastName[0]) ? lastName : "\xe2\x80\x94"); // —
    }
    p.print('"');

    commaOut(p, needComma);
    p.print("\"currentProgramName\":");
    if (st.program->isRunning()) {
        p.print('"');
        escapeJsonString(p, st.program->getCurrentProgramName());
        p.print('"');
    } else {
        p.print("null");
    }

    commaOut(p, needComma);
    p.print("\"timerRemaining\":");
    {
        const uint32_t remMs = st.program->getTimerRemainingMs();
        const uint32_t remSec = msToSecCeil(remMs);
        p.print(remSec);
    }

    commaOut(p, needComma);
    p.print("\"programRunning\":");
    p.print(st.program->isRunning() ? "true" : "false");
}

void emitProgramPayload(Print& p, const SseStatusPort& st) {
    if (!st.program) return;
    bool c = false;
    p.print('{');
    emitProgramFields(p, &c, st);
    p.print('}');
}

static void emitFlashCommitObject(Print& p, const SseStatusPort& st) {
    if (!st.flash) return;
    p.print('{');
    bool inner = false;
    commaOut(p, &inner);
    p.print("\"pending\":");
    p.print(st.flash->isPending() ? "true" : "false");

    const char* lastOp = "none";
    switch (st.flash->getLastFlashOp()) {
        case FlashCommitOp::SAVE_CONFIG:
            lastOp = "save_config";
            break;
        case FlashCommitOp::RESET_CONFIG:
            lastOp = "reset_config";
            break;
        case FlashCommitOp::SAVE_PROGRAM:
            lastOp = "save_program";
            break;
        case FlashCommitOp::DELETE_PROGRAM:
            lastOp = "delete_program";
            break;
        case FlashCommitOp::RESET_PROGRAMS:
            lastOp = "reset_programs";
            break;
        default:
            break;
    }
    commaOut(p, &inner);
    p.print("\"lastOp\":\"");
    escapeJsonString(p, lastOp);
    p.print('"');
    commaOut(p, &inner);
    p.print("\"lastOk\":");
    p.print(st.flash->getLastFlashOk() ? "true" : "false");
    commaOut(p, &inner);
    p.print("\"lastMillis\":");
    p.print(static_cast<unsigned long>(st.flash->getLastFlashMillis()));
    p.print('}');
}

void emitFlashPayload(Print& p, const SseStatusPort& st) {
    if (!st.flash) return;
    bool fk = false;
    p.print('{');
    commaOut(p, &fk);
    p.print("\"flashCommit\":");
    emitFlashCommitObject(p, st);
    p.print('}');
}

void emitActiveErrorsArray(Print& p, const SseStatusPort& st) {
    p.print('[');
    if (st.errors) {
        ErrorSnapshotEntry buf[ErrorHistory::CAPACITY]{};
        const uint8_t n = st.errors->copyActive(buf, ErrorHistory::CAPACITY);
        for (uint8_t i = 0; i < n; i++) {
            if (i) p.print(',');
            p.print("{\"code\":");
            p.print((unsigned)buf[i].code);
            p.print(",\"msg\":\"");
            escapeJsonString(p, buf[i].msg);
            p.print("\",\"active\":");
            p.print(buf[i].active ? "true" : "false");
            p.print('}');
        }
    }
    p.print(']');
}

void emitClocksPayload(Print& p, const SseStatusPort& st) {
    const uint32_t remMs = st.program ? st.program->getTimerRemainingMs() : 0;
    const uint32_t remSec = msToSecCeil(remMs);
    const bool prog = st.program && st.program->isRunning();
    bool c = false;
    p.print('{');
    commaOut(p, &c);
    p.print("\"uptime\":");
    p.print(static_cast<unsigned long>(st.uptimeSec));
    commaOut(p, &c);
    p.print("\"timerRemaining\":");
    p.print(remSec);
    commaOut(p, &c);
    p.print("\"programRunning\":");
    p.print(prog ? "true" : "false");
    commaOut(p, &c);
    p.print("\"freeHeap\":");
    p.print(static_cast<unsigned long>(st.freeHeap));
    commaOut(p, &c);
    p.print("\"activeErrors\":");
    emitActiveErrorsArray(p, st);
    commaOut(p, &c);
    p.print("\"epoch\":");
    p.print(static_cast<unsigned long>(st.epochUtc));
    commaOut(p, &c);
    p.print("\"synced\":");
    p.print(st.timeSynced ? "true" : "false");
    commaOut(p, &c);
    p.print("\"stale\":");
    p.print(st.timeStale ? "true" : "false");
    commaOut(p, &c);
    p.print("\"tzOffsetHours\":");
    p.print(static_cast<long>(st.tzOffsetHours));
    commaOut(p, &c);
    p.print("\"timeSource\":\"");
    escapeJsonString(p, st.timeSource);
    p.print('"');
    p.print('}');
}

void emitSnapshotPayload(Print& p, const SseStatusPort& st) {
    bool c = false;
    p.print('{');

    commaOut(p, &c);
    p.print("\"mode\":\"");
    escapeJsonString(p, st.modeName);
    p.print('"');

    commaOut(p, &c);
    p.print("\"uptime\":");
    p.print(static_cast<unsigned long>(st.uptimeSec));

    commaOut(p, &c);
    p.print("\"activeErrors\":");
    emitActiveErrorsArray(p, st);

    commaOut(p, &c);
    p.print("\"engineRunning\":");
    p.print(st.engineRunning ? "true" : "false");

    commaOut(p, &c);
    p.print("\"gsmState\":\"");
    escapeJsonString(p, st.gsm ? st.gsm->getStateString() : "");
    p.print('"');

    commaOut(p, &c);
    p.print("\"csq\":{\"rssi\":");
    p.print((int)st.csqRssi);
    p.print(",\"ber\":");
    p.print((int)st.csqBer);
    p.print('}');

    commaOut(p, &c);
    p.print("\"mqttConnected\":");
    p.print(st.mqttConnected ? "true" : "false");

    emitRelayInputTempVoltageMaps(p, &c, st);
    emitProgramFields(p, &c, st);

    commaOut(p, &c);
    p.print("\"runtime\":");
    emitRuntimeObject(p, st);

    if (st.flash) {
        commaOut(p, &c);
        p.print("\"flashCommit\":");
        emitFlashCommitObject(p, st);
    }

    p.print('}');
}

} // namespace sse_inc_detail
