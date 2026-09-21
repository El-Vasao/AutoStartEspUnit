/**
 * @file WebServer.SseIncremental.cpp
 * @brief Инкрементальный SSE + стрим живого состояния в GET /bootstrap (ключ live). Resync SSE вместо толстого snapshot.
 */
#include "web/WebServer.h"
#include "web/internal/WebServerRuntime.h"

#include "config/Config.h"
#include "common/Constants.h"
#include "common/EspHal.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "core/FlashCommitCoordinator.h"
#include "gsm/GSMController.h"
#include "io/DigitalInputs.h"
#include "io/RelayController.h"
#include "io/SensorsController.h"
#include "program/ProgramExecutor.h"
#include <cstring>

namespace sse_inc_detail {

constexpr size_t kPayloadCap = JsonBytes::Web::SSE_STATUS_JSON_MAX;
char gSsePayload[kPayloadCap];

static uint32_t fnv1a32(const uint8_t* data, size_t len) {
    constexpr uint32_t kFnvPrime = 16777619U;
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= kFnvPrime;
    }
    return hash;
}

/// Writes SSE JSON payloads into NUL-terminated capped buffer (`gSsePayload`).
class PayloadPrint : public Print {
public:
    explicit PayloadPrint() : buf_(gSsePayload), cap_(kPayloadCap), len_(0), truncated_(false) {}

    size_t write(uint8_t c) override {
        if (len_ + 1 >= cap_) {
            truncated_ = true;
            return 0;
        }
        buf_[len_++] = static_cast<char>(c);
        return 1;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        if (!buffer || size == 0) return 0;
        const size_t room = (cap_ > len_ + 1) ? (cap_ - 1 - len_) : 0;
        if (room == 0) {
            truncated_ = true;
            return 0;
        }
        const size_t n = (size < room) ? size : room;
        memcpy(buf_ + len_, buffer, n);
        len_ += n;
        if (n < size) truncated_ = true;
        return n;
    }

    size_t length() const { return len_; }
    bool truncated() const { return truncated_; }
    char* data() { return buf_; }

    bool seal() {
        if (len_ >= cap_) return false;
        buf_[len_] = '\0';
        return true;
    }

private:
    char* buf_;
    size_t cap_;
    size_t len_;
    bool truncated_;
};

static inline void commaOut(Print& p, bool* needComma) {
    if (*needComma) p.print(',');
    *needComma = true;
}

static void escapeJsonString(Print& p, const char* s) {
    if (!s) return;
    for (; *s; s++) {
        const char ch = *s;
        switch (ch) {
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
                p.write(static_cast<uint8_t>(ch));
                break;
        }
    }
}

/// --- Static dedup state (invalidated on resync / полный HTTP live). -----------------------------
static uint32_t gLastClkMsSent{0};
static uint32_t gLastHardwareHash{0};
static uint32_t gLastRuntimeHash{0};
static uint32_t gLastProgramHash{0};
static char gLastMode[TextBytes::Wifi::SSID]{};
static char gLastGsm[64]{}; // GSM state string is short; keep off hot RX buffer constants
static char gLastErr[Logging::MAX_MESSAGE_LENGTH]{};
static FlashCommitOp gLastFlashOp{FlashCommitOp::NONE};
static bool gLastFlashPending{false};
static bool gLastFlashOk{true};
static uint32_t gLastFlashMillisVal{0};
/// After SSE connect: emit clocks→mode→gsm→hardware in one tick while soft queue allows.
static uint8_t gBaselineStep{0}; // 0=idle/done, 1=clocks, 2=mode, 3=gsm, 4=hardware

static void invalidateDedupState() {
    gLastHardwareHash = 0;
    gLastRuntimeHash = 0;
    gLastProgramHash = 0;
    gLastMode[0] = '\0';
    gLastGsm[0] = '\0';
    gLastErr[0] = '\0';
    gLastFlashOp = FlashCommitOp::NONE;
    gLastFlashPending = false;
    gLastFlashOk = true;
    gLastFlashMillisVal = 0;
}

/// New SSE client: clear dedup and request baseline burst (no HTTP live required).
static void requestBaselineResync() {
    invalidateDedupState();
    gLastClkMsSent = 0;
    gBaselineStep = 1;
}

/** True if another SSE enqueue this tick is unlikely to hit ESPAsync hard queue discard. */
static bool sseTickMoreEventsSafe(WebServer& ws) {
#if defined(SSE_MAX_QUEUED_MESSAGES)
    constexpr size_t cap = SSE_MAX_QUEUED_MESSAGES;
#else
    constexpr size_t cap = 12;
#endif
    constexpr size_t thr = (cap > 5) ? (cap - 3) : 1;
    return !WebServerRuntime::sseQueueBackpressureAtLeast(ws, thr);
}

static bool sendJsonEvent(WebServer& ws, const char* sseEventName, PayloadPrint& builder, size_t maxQueueDepth) {
    if (!WebServerRuntime::sseSoftQueueAllowsSend(ws, maxQueueDepth)) return false;
    if (!builder.truncated() && builder.length() > 0 && builder.length() < kPayloadCap) {
        if (!builder.seal()) return false;
        WebServerRuntime::sseSendEvent(ws, gSsePayload, sseEventName, millis());
        return true;
    }
    return false;
}

/// Relays, inputs, frequencies, temps, voltage — общий блок для hardware-события и snapshot.
static void emitRelayInputTempVoltageMaps(Print& p, bool* needComma) {
    commaOut(p, needComma);
    p.print("\"relaysById\":{");
    {
        bool rk = false;
        for (int i = 0; i < HardwareLimits::RELAYS; i++) {
            commaOut(p, &rk);
            char kb[16];
            snprintf(kb, sizeof kb, "\"%u\":", (unsigned)Pin::RELAY_IDS[i]);
            p.print(kb);
            p.print(core.getRelay().getState(i) ? "true" : "false");
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
            p.print(core.getInputs().getState(i) ? "true" : "false");
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
            if (core.getInputs().isCounter(i)) {
                p.print(core.getInputs().getFrequency(i));
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
            p.print(core.getInputs().isRuntimeEnabled(i) ? "true" : "false");
        }
    }
    p.print('}');

    commaOut(p, needComma);
    p.print("\"tempSensors\":[");
    for (int i = 0; i < HardwareLimits::SENSORS; i++) {
        if (i != 0) p.print(',');
        p.print('{');
        bool tc = false;

        uint16_t sid = 0;
        const uint8_t* addr = core.getSensors().getSensorAddress(i);
        if (addr) {
            char romStr[TextBytes::Sensors::ADDR_STRING];
            snprintf(romStr, sizeof romStr, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                     addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7]);
            for (uint8_t j = 0; j < HardwareLimits::SENSORS; j++) {
                const auto& sc = config.getBase().sensors[j];
                if (sc.rom[0] == '\0' || sc.id == 0) continue;
                if (strcmp(sc.rom, romStr) == 0) {
                    sid = sc.id;
                    break;
                }
            }
        }

        commaOut(p, &tc);
        p.print("\"id\":");
        if (sid)
            p.print(sid);
        else
            p.print("null");

        const bool valid = core.getSensors().isTemperatureValid(i);
        commaOut(p, &tc);
        p.print("\"valid\":");
        p.print(valid ? "true" : "false");

        commaOut(p, &tc);
        p.print("\"lastMs\":");
        p.print(static_cast<unsigned long>(core.getSensors().getLastTemperatureTime(i)));

        commaOut(p, &tc);
        p.print("\"t\":");
        if (valid)
            p.print(core.getSensors().getTemperature(i));
        else
            p.print("null");

        p.print('}');
    }
    p.print(']');

    commaOut(p, needComma);
    p.print("\"voltage\":");
    if (core.getSensors().isVoltageValid())
        p.print(core.getSensors().getVoltage());
    else
        p.print("null");
}

static void emitHardwarePayload(Print& p) {
    bool c = false;
    p.print('{');
    emitRelayInputTempVoltageMaps(p, &c);
    commaOut(p, &c);
    p.print("\"engineRunning\":");
    p.print(core.isEngineRunning() ? "true" : "false");
    p.print('}');
}

static void emitRuntimePayload(Print& p) {
    p.print('{');
    p.print("\"runtime\":{");
    {
        bool rr = false;
        commaOut(p, &rr);
        p.print("\"thermostat\":");
        p.print(core.getThermostatRuntime() ? "true" : "false");
        commaOut(p, &rr);
        p.print("\"batterySaver\":");
        p.print(core.getBatterySaverRuntime() ? "true" : "false");
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
                p.print(core.getTriggerRuntime(i) ? "true" : "false");
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
                p.print(core.getTempTriggerRuntime(i) ? "true" : "false");
            }
        }
        p.print('}');
    }
    p.print('}');
    p.print('}');
}

static void emitProgramPayload(Print& p) {
    bool c = false;
    p.print('{');
    commaOut(p, &c);
    p.print("\"lastProgramName\":\"");
    {
        const char* lastName = core.getProgramExecutor().getLastProgramName();
        escapeJsonString(p, (lastName && lastName[0]) ? lastName : "\xe2\x80\x94"); // —
    }
    p.print('"');

    commaOut(p, &c);
    p.print("\"currentProgramName\":");
    if (core.getProgramExecutor().isRunning()) {
        p.print('"');
        escapeJsonString(p, core.getProgramExecutor().getCurrentProgramName());
        p.print('"');
    } else {
        p.print("null");
    }

    commaOut(p, &c);
    p.print("\"timerRemaining\":");
    {
        const uint32_t remMs = core.getProgramExecutor().getTimerRemainingMs();
        const uint32_t remSec = remMs ? (remMs + 999) / 1000 : 0;
        p.print(remSec);
    }

    commaOut(p, &c);
    p.print("\"programRunning\":");
    p.print(core.getProgramExecutor().isRunning() ? "true" : "false");

    p.print('}');
}

static void emitFlashPayload(Print& p) {
    bool fk = false;
    p.print('{');
    commaOut(p, &fk);
    p.print("\"flashCommit\":{");
    {
        bool inner = false;
        commaOut(p, &inner);
        p.print("\"pending\":");
        p.print(flashCommit.isPending() ? "true" : "false");

        const char* lastOp = "none";
        switch (flashCommit.getLastFlashOp()) {
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
        p.print(flashCommit.getLastFlashOk() ? "true" : "false");
        commaOut(p, &inner);
        p.print("\"lastMillis\":");
        p.print(static_cast<unsigned long>(flashCommit.getLastFlashMillis()));
    }
    p.print('}');
    p.print('}');
}

static void emitClocksPayload(Print& p) {
    const uint32_t remMs = core.getProgramExecutor().getTimerRemainingMs();
    const uint32_t remSec = remMs ? (remMs + 999) / 1000 : 0;
    const bool prog = core.getProgramExecutor().isRunning();
    bool c = false;
    p.print('{');
    commaOut(p, &c);
    p.print("\"uptime\":");
    p.print(static_cast<unsigned long>(core.getUptime()));
    commaOut(p, &c);
    p.print("\"timerRemaining\":");
    p.print(remSec);
    commaOut(p, &c);
    p.print("\"programRunning\":");
    p.print(prog ? "true" : "false");
    commaOut(p, &c);
    p.print("\"freeHeap\":");
    p.print(static_cast<unsigned long>(espHalFreeHeap()));
    commaOut(p, &c);
    p.print("\"lastError\":\"");
    escapeJsonString(p, core.getErrorManager().getMessage());
    p.print('"');
    p.print('}');
}

void emitSnapshotPayload(Print& p) {
    bool c = false;
    p.print('{');

    commaOut(p, &c);
    p.print("\"mode\":\"");
    escapeJsonString(p, core.getModeName());
    p.print('"');

    commaOut(p, &c);
    p.print("\"uptime\":");
    p.print(static_cast<unsigned long>(core.getUptime()));

    commaOut(p, &c);
    p.print("\"lastError\":\"");
    escapeJsonString(p, core.getErrorManager().getMessage());
    p.print('"');

    commaOut(p, &c);
    p.print("\"engineRunning\":");
    p.print(core.isEngineRunning() ? "true" : "false");

    commaOut(p, &c);
    p.print("\"gsmState\":\"");
    escapeJsonString(p, core.getGSM().getStateString());
    p.print('"');

    emitRelayInputTempVoltageMaps(p, &c);

    commaOut(p, &c);
    p.print("\"lastProgramName\":\"");
    {
        const char* lastName = core.getProgramExecutor().getLastProgramName();
        escapeJsonString(p, (lastName && lastName[0]) ? lastName : "\xe2\x80\x94");
    }
    p.print('"');

    commaOut(p, &c);
    p.print("\"currentProgramName\":");
    if (core.getProgramExecutor().isRunning()) {
        p.print('"');
        escapeJsonString(p, core.getProgramExecutor().getCurrentProgramName());
        p.print('"');
    } else {
        p.print("null");
    }

    commaOut(p, &c);
    p.print("\"timerRemaining\":");
    {
        const uint32_t remMs = core.getProgramExecutor().getTimerRemainingMs();
        const uint32_t remSec = remMs ? (remMs + 999) / 1000 : 0;
        p.print(remSec);
    }

    commaOut(p, &c);
    p.print("\"programRunning\":");
    p.print(core.getProgramExecutor().isRunning() ? "true" : "false");

    commaOut(p, &c);
    p.print("\"runtime\":{");
    {
        bool rr = false;
        commaOut(p, &rr);
        p.print("\"thermostat\":");
        p.print(core.getThermostatRuntime() ? "true" : "false");
        commaOut(p, &rr);
        p.print("\"batterySaver\":");
        p.print(core.getBatterySaverRuntime() ? "true" : "false");
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
                p.print(core.getTriggerRuntime(i) ? "true" : "false");
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
                p.print(core.getTempTriggerRuntime(i) ? "true" : "false");
            }
        }
        p.print('}');
    }
    p.print('}');

    commaOut(p, &c);
    p.print("\"flashCommit\":{");
    {
        bool inner = false;
        commaOut(p, &inner);
        p.print("\"pending\":");
        p.print(flashCommit.isPending() ? "true" : "false");

        const char* lastOp = "none";
        switch (flashCommit.getLastFlashOp()) {
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
        p.print(flashCommit.getLastFlashOk() ? "true" : "false");
        commaOut(p, &inner);
        p.print("\"lastMillis\":");
        p.print(static_cast<unsigned long>(flashCommit.getLastFlashMillis()));
    }
    p.print('}');

    p.print('}');
}

/// Force status: pace baseline re-emit (same path as new SSE client). No HTTP live required.
static void broadcastResync(WebServer& ws, size_t maxQueueDepth) {
    (void)maxQueueDepth;
    if (!WebServerRuntime::sseActiveUiOk(ws)) return;
    if (WebServerRuntime::refreshSseClientCount(ws) == 0) return;
    // Optional resync signal; FE waits for baseline events.
    if (sseTickMoreEventsSafe(ws) && !WebServerRuntime::sseQueueBackpressureAtLeast(ws, WebSseLimits::STATUS_QUEUE_MAX)) {
        WebServerRuntime::sseSendEvent(ws, "{}", "resync", millis());
    }
    requestBaselineResync();
}

static void runSseIncrementalTick(WebServer& ws, uint32_t now) {
    const size_t statusQueueMax = WebSseLimits::STATUS_QUEUE_MAX;
    if (!WebServerRuntime::sseActiveUiOk(ws)) return;
    if (WebServerRuntime::refreshSseClientCount(ws) == 0) return;
    if (WebServerRuntime::sseQueueBackpressureAtLeast(ws, statusQueueMax)) return;

    // Разгрузить lwIP перед пачкой `events.send`.
    yield();

    // Baseline after connect: emit clocks→mode→gsm→hardware in one tick while queue allows.
    while (gBaselineStep >= 1 && gBaselineStep <= 4) {
        if (!sseTickMoreEventsSafe(ws)) return;
        if (gBaselineStep == 1) {
            gLastClkMsSent = now;
            PayloadPrint cp;
            emitClocksPayload(cp);
            if (sendJsonEvent(ws, "clocks", cp, statusQueueMax)) {
                gBaselineStep = 2;
            } else {
                return;
            }
            continue;
        }
        if (gBaselineStep == 2) {
            const char* curMode = core.getModeName();
            PayloadPrint mp;
            mp.print('{');
            bool cm = false;
            commaOut(mp, &cm);
            mp.print("\"mode\":\"");
            escapeJsonString(mp, curMode);
            mp.print('"');
            mp.print('}');
            if (sendJsonEvent(ws, "mode", mp, statusQueueMax)) {
                strlcpy(gLastMode, curMode, sizeof(gLastMode));
                gBaselineStep = 3;
            } else {
                return;
            }
            continue;
        }
        if (gBaselineStep == 3) {
            const char* curGsm = core.getGSM().getStateString();
            PayloadPrint gp;
            gp.print('{');
            bool cg = false;
            commaOut(gp, &cg);
            gp.print("\"gsmState\":\"");
            escapeJsonString(gp, curGsm);
            gp.print('"');
            gp.print('}');
            if (sendJsonEvent(ws, "gsm", gp, statusQueueMax)) {
                strlcpy(gLastGsm, curGsm, sizeof(gLastGsm));
                gBaselineStep = 4;
            } else {
                return;
            }
            continue;
        }
        // step 4: hardware
        {
            PayloadPrint hp;
            emitHardwarePayload(hp);
            if (!hp.truncated() && hp.length() && hp.seal()) {
                const uint32_t h = fnv1a32(reinterpret_cast<const uint8_t*>(gSsePayload), hp.length());
                if (WebServerRuntime::sseSoftQueueAllowsSend(ws, statusQueueMax)) {
                    WebServerRuntime::sseSendEvent(ws, gSsePayload, "hardware", millis());
                    gLastHardwareHash = h;
                    gBaselineStep = 0;
                } else {
                    return;
                }
            } else {
                gBaselineStep = 0; // skip if truncated; normal path may retry later
            }
        }
    }

    if (gBaselineStep != 0) return;

    // Clocks: ~1 Hz — лёгкий keepalive для EventSource stale detector.
    if (gLastClkMsSent == 0 || (uint32_t)(now - gLastClkMsSent) >= Timing::SSE_CLOCKS_INTERVAL_MS) {
        if (!sseTickMoreEventsSafe(ws)) return;
        gLastClkMsSent = now;
        PayloadPrint cp;
        emitClocksPayload(cp);
        sendJsonEvent(ws, "clocks", cp, statusQueueMax);
    }

    const char* curMode = core.getModeName();
    if (strcmp(gLastMode, curMode) != 0) {
        if (!sseTickMoreEventsSafe(ws)) return;
        PayloadPrint mp;
        mp.print('{');
        bool cm = false;
        commaOut(mp, &cm);
        mp.print("\"mode\":\"");
        escapeJsonString(mp, curMode);
        mp.print('"');
        mp.print('}');
        if (sendJsonEvent(ws, "mode", mp, statusQueueMax)) {
            strlcpy(gLastMode, curMode, sizeof(gLastMode));
        }
    }

    const char* curGsm = core.getGSM().getStateString();
    if (strcmp(gLastGsm, curGsm) != 0) {
        if (!sseTickMoreEventsSafe(ws)) return;
        PayloadPrint gp;
        gp.print('{');
        bool cg = false;
        commaOut(gp, &cg);
        gp.print("\"gsmState\":\"");
        escapeJsonString(gp, curGsm);
        gp.print('"');
        gp.print('}');
        if (sendJsonEvent(ws, "gsm", gp, statusQueueMax)) {
            strlcpy(gLastGsm, curGsm, sizeof(gLastGsm));
        }
    }

    const char* curErr = core.getErrorManager().getMessage();
    if (strcmp(gLastErr, curErr) != 0) {
        if (!sseTickMoreEventsSafe(ws)) return;
        PayloadPrint ep;
        ep.print('{');
        bool ce = false;
        commaOut(ep, &ce);
        ep.print("\"lastError\":\"");
        escapeJsonString(ep, curErr);
        ep.print('"');
        ep.print('}');
        if (sendJsonEvent(ws, "error", ep, statusQueueMax)) {
            strlcpy(gLastErr, curErr, sizeof(gLastErr));
        }
    }

    {
        if (!sseTickMoreEventsSafe(ws)) return;
        PayloadPrint hp;
        emitHardwarePayload(hp);
        if (!hp.truncated() && hp.length() && hp.seal()) {
            const uint32_t h = fnv1a32(reinterpret_cast<const uint8_t*>(gSsePayload), hp.length());
            if (h != gLastHardwareHash && WebServerRuntime::sseSoftQueueAllowsSend(ws, statusQueueMax)) {
                WebServerRuntime::sseSendEvent(ws, gSsePayload, "hardware", millis());
                gLastHardwareHash = h;
            }
        }
    }

    {
        if (!sseTickMoreEventsSafe(ws)) return;
        PayloadPrint rp;
        emitRuntimePayload(rp);
        if (!rp.truncated() && rp.length() && rp.seal()) {
            const uint32_t h = fnv1a32(reinterpret_cast<const uint8_t*>(gSsePayload), rp.length());
            if (h != gLastRuntimeHash && WebServerRuntime::sseSoftQueueAllowsSend(ws, statusQueueMax)) {
                WebServerRuntime::sseSendEvent(ws, gSsePayload, "runtime", millis());
                gLastRuntimeHash = h;
            }
        }
    }

    {
        if (!sseTickMoreEventsSafe(ws)) return;
        PayloadPrint pp;
        emitProgramPayload(pp);
        if (!pp.truncated() && pp.length() && pp.seal()) {
            const uint32_t h = fnv1a32(reinterpret_cast<const uint8_t*>(gSsePayload), pp.length());
            if (h != gLastProgramHash && WebServerRuntime::sseSoftQueueAllowsSend(ws, statusQueueMax)) {
                WebServerRuntime::sseSendEvent(ws, gSsePayload, "program", millis());
                gLastProgramHash = h;
            }
        }
    }

    const bool fp = flashCommit.isPending();
    const FlashCommitOp fo = flashCommit.getLastFlashOp();
    const bool fok = flashCommit.getLastFlashOk();
    const uint32_t fms = flashCommit.getLastFlashMillis();
    if (fp != gLastFlashPending || fo != gLastFlashOp || fok != gLastFlashOk || fms != gLastFlashMillisVal) {
        if (!sseTickMoreEventsSafe(ws)) return;
        PayloadPrint fpw;
        emitFlashPayload(fpw);
        if (sendJsonEvent(ws, "flash", fpw, statusQueueMax)) {
            gLastFlashPending = fp;
            gLastFlashOp = fo;
            gLastFlashOk = fok;
            gLastFlashMillisVal = fms;
        }
    }
}

} // namespace sse_inc_detail

void WebServerRuntime::emitLiveSnapshotJson(Print& p) {
    sse_inc_detail::emitSnapshotPayload(p);
}

void WebServerRuntime::broadcastStatusForce(WebServer& ws) {
    sse_inc_detail::broadcastResync(ws, WebSseLimits::STATUS_FORCE_QUEUE_MAX);
}

void WebServerRuntime::requestSseIncrementalBaseline(WebServer& ws) {
    (void)ws;
    sse_inc_detail::requestBaselineResync();
}

void WebServerRuntime::sendStatus(WebServer& ws, size_t maxQueueDepth) {
    sse_inc_detail::broadcastResync(ws, maxQueueDepth);
}

void WebServerRuntime::tickSseIncremental(WebServer& ws, uint32_t now) {
    sse_inc_detail::runSseIncrementalTick(ws, now);
}
