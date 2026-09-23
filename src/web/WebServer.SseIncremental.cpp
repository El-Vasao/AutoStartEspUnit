/**
 * @file WebServer.SseIncremental.cpp
 * @brief SSE incremental tick orchestration (dedup + baseline + queue gates).
 * Domain JSON builders live in WebServer.SseBuilders.cpp behind SseStatusPort.
 */
#include "web/WebServer.h"
#include "web/internal/WebServerRuntime.h"
#include "web/internal/SseIncCommon.h"
#include "web/internal/SseStatusPort.h"

#include "common/Constants.h"
#include "common/EspHal.h"
#include "core/Core.h"
#include "core/TimeSyncManager.h"
#include "core/ErrorManager.h"
#include "core/FlashCommitCoordinator.h"
#include "gsm/GSMController.h"

#include <cstring>

namespace sse_inc_detail {

/// --- Static dedup state (invalidated on resync / полный HTTP live). -----------------------------
static uint32_t gLastClkMsSent{0};
static uint32_t gLastHardwareHash{0};
static uint32_t gLastRuntimeHash{0};
static uint32_t gLastProgramHash{0};
static char gLastMode[TextBytes::Wifi::SSID]{};
static char gLastGsm[64]{};
static char gLastErr[Logging::MAX_MESSAGE_LENGTH]{};
static FlashCommitOp gLastFlashOp{FlashCommitOp::NONE};
static bool gLastFlashPending{false};
static bool gLastFlashOk{true};
static uint32_t gLastFlashMillisVal{0};
/// After SSE connect: emit clocks→mode→gsm→hardware in one tick while soft queue allows.
static uint8_t gBaselineStep{0}; // 0=idle/done, 1=clocks, 2=mode, 3=gsm, 4=hardware

static bool triggerRuntimeThunk(void* ctx, uint8_t index) {
    return static_cast<Core*>(ctx)->getTriggerRuntime(index);
}

static bool tempTriggerRuntimeThunk(void* ctx, uint8_t index) {
    return static_cast<Core*>(ctx)->getTempTriggerRuntime(index);
}

static SseStatusPort makeStatusPort() {
    SseStatusPort st;
    st.relay = &core.getRelay();
    st.inputs = &core.getInputs();
    st.sensors = &core.getSensors();
    st.program = &core.getProgramExecutor();
    st.gsm = &core.getGSM();
    st.errors = &core.getErrorManager();
    st.flash = &flashCommit;
    st.modeName = core.getModeName();
    st.uptimeSec = core.getUptime();
    st.freeHeap = espHalFreeHeap();
    st.engineRunning = core.isEngineRunning();
    st.thermostatRuntime = core.getThermostatRuntime();
    st.batterySaverRuntime = core.getBatterySaverRuntime();
    {
        const auto& ts = core.getTimeSync();
        st.timeSynced = ts.isSynced();
        st.timeStale = ts.isStale();
        st.epochUtc = st.timeSynced ? static_cast<uint32_t>(ts.epochUtc()) : 0;
        st.tzOffsetHours = ts.tzOffsetHours();
    }
    st.getTriggerRuntime = triggerRuntimeThunk;
    st.getTempTriggerRuntime = tempTriggerRuntimeThunk;
    st.triggerCtx = &core;
    return st;
}

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

/// Force status: pace baseline re-emit (same path as new SSE client). No HTTP live required.
static void broadcastResync(WebServer& ws, size_t maxQueueDepth) {
    (void)maxQueueDepth;
    if (!WebServerRuntime::sseActiveUiOk(ws)) return;
    if (WebServerRuntime::refreshSseClientCount(ws) == 0) return;
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

    yield();

    const SseStatusPort st = makeStatusPort();

    while (gBaselineStep >= 1 && gBaselineStep <= 4) {
        if (!sseTickMoreEventsSafe(ws)) return;
        if (gBaselineStep == 1) {
            gLastClkMsSent = now;
            PayloadPrint cp;
            emitClocksPayload(cp, st);
            if (sendJsonEvent(ws, "clocks", cp, statusQueueMax)) {
                gBaselineStep = 2;
            } else {
                return;
            }
            continue;
        }
        if (gBaselineStep == 2) {
            const char* curMode = st.modeName;
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
            const char* curGsm = st.gsm ? st.gsm->getStateString() : "";
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
        {
            PayloadPrint hp;
            emitHardwarePayload(hp, st);
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
                gBaselineStep = 0;
            }
        }
    }

    if (gBaselineStep != 0) return;

    if (gLastClkMsSent == 0 || (uint32_t)(now - gLastClkMsSent) >= Timing::SSE_CLOCKS_INTERVAL_MS) {
        if (!sseTickMoreEventsSafe(ws)) return;
        gLastClkMsSent = now;
        PayloadPrint cp;
        emitClocksPayload(cp, st);
        sendJsonEvent(ws, "clocks", cp, statusQueueMax);
    }

    const char* curMode = st.modeName;
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

    const char* curGsm = st.gsm ? st.gsm->getStateString() : "";
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

    const char* curErr = st.errors ? st.errors->getMessage() : "OK";
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
        emitHardwarePayload(hp, st);
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
        emitRuntimePayload(rp, st);
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
        emitProgramPayload(pp, st);
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
        emitFlashPayload(fpw, st);
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
    sse_inc_detail::emitSnapshotPayload(p, sse_inc_detail::makeStatusPort());
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
