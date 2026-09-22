// src/core/Core.Core.cpp
#include "core/Core.h"
#include "common/EspHal.h"
#include "core/internal/CorePrivate.h"

#include "config/Config.h"
#include "config/DefaultConfig.h"
#include "common/Logger.h"
#include "common/Constants.h"
#include "fs/FSManager.h"
#include "web/WebServer.h"
#include "core/FlashCommitCoordinator.h"
#include "core/CoreHardRestart.h"

#include <WiFi.h>

Core core;
CorePrivate g_coreImpl;

namespace {
static void wdtFeedThunk(void* ctx) {
    static_cast<Core*>(ctx)->feedWatchdog();
}

static void cooperateThunk(void* ctx) {
    static_cast<Core*>(ctx)->cooperate();
}

static void requestRebootThunk(void* ctx, uint32_t delayMs) {
    static_cast<Core*>(ctx)->requestReboot(delayMs);
}

static bool startProgramThunk(void* ctx, uint8_t programId) {
    return static_cast<Core*>(ctx)->getProgramExecutor().start(programId);
}

static void stopProgramThunk(void* ctx) {
    static_cast<Core*>(ctx)->getProgramExecutor().stop();
}

static void startOtaUpdateThunk(void* ctx) {
    static_cast<Core*>(ctx)->startOTAUpdate();
}

static void factoryResetThunk(void* ctx) {
    static_cast<Core*>(ctx)->factoryReset();
}

static void setThermostatThunk(void* ctx, bool en) {
    static_cast<Core*>(ctx)->setThermostatRuntime(en);
}

static void setBatterySaverThunk(void* ctx, bool en) {
    static_cast<Core*>(ctx)->setBatterySaverRuntime(en);
}

static bool setInputRuntimeThunk(void* ctx, uint16_t id, bool en) {
    Core* c = static_cast<Core*>(ctx);
    const int8_t idx = c->getInputs().findIndexById(id);
    if (idx < 0) return false;
    c->getInputs().setRuntimeEnabled((uint8_t)idx, en);
    return true;
}

static bool setInputTriggerThunk(void* ctx, uint16_t id, bool en) {
    Core* c = static_cast<Core*>(ctx);
    const auto& cfg = config.getBase();
    for (uint8_t i = 0; i < cfg.input_triggers_count && i < Limits::MAX_TRIGGERS; i++) {
        if (cfg.input_triggers[i].id == id) {
            c->setTriggerRuntime(i, en);
            return true;
        }
    }
    return false;
}

static bool setTempTriggerThunk(void* ctx, uint16_t id, bool en) {
    Core* c = static_cast<Core*>(ctx);
    const auto& cfg = config.getBase();
    for (uint8_t i = 0; i < cfg.temperature_triggers_count && i < Limits::MAX_TRIGGERS; i++) {
        if (cfg.temperature_triggers[i].id == id) {
            c->setTempTriggerRuntime(i, en);
            return true;
        }
    }
    return false;
}

static void logHeapTag_(const char* tag) {
    logger.log("[Core] heap %s: free=%u maxBlk=%u frag=%u%%\n", tag, (unsigned)espHalFreeHeap(),
               (unsigned)espHalMaxBlock(), (unsigned)0 /* heap frag N/A on ESP32 */);
}

static void drainDeferredOtaFromWebUpload_(CorePrivate& impl) {
    if (!impl.pendingDeferredOtaFromWebUpload) return;
    impl.pendingDeferredOtaFromWebUpload = false;
    if (impl.modeManager.getCurrentMode() != CoreMode::NORMAL) {
        return;
    }
    logHeapTag_("before deferred OTA switch");
    // Stream FSM already started in onOtaHttpUploadStreamOpenedFromWeb — do not reset it.
    impl.modeManager.switchMode(CoreMode::OTA_UPDATE);
    logHeapTag_("after deferred OTA switch");
}

} // namespace

Core::Core()
    : _impl(&g_coreImpl) {
    _modeHandlers[static_cast<size_t>(CoreMode::BOOT)] = &Core::handleBoot;
    _modeHandlers[static_cast<size_t>(CoreMode::EMERGENCY_AP)] = &Core::handleEmergencyAP;
    _modeHandlers[static_cast<size_t>(CoreMode::SETUP_AP)] = &Core::handleSetupAP;
    _modeHandlers[static_cast<size_t>(CoreMode::NORMAL)] = &Core::handleNormal;
    _modeHandlers[static_cast<size_t>(CoreMode::NORMAL_SILENT)] = &Core::handleNormalSilent;
    _modeHandlers[static_cast<size_t>(CoreMode::OTA_UPDATE)] = &Core::handleOTAUpdate;
}

bool Core::begin() {
    CorePrivate& impl = *_impl;
    impl.pendingHardRestart = false;
    impl.pendingDeferredOtaFromWebUpload = false;
    impl.bootStage = CorePrivate::BootStage::InitWiFiOff;
    impl.bootTargetMode = CoreMode::BOOT;
    impl.bootConfigLoaded = false;
    impl.bootHwMapBad = false;

    logger.log("\n[Core] begin\n");
    logger.log("[Core] Reset reason: %s\n", getResetReason());
    if (!espHalChipIdValid()) return false;

    espHalWdtEnable();
    impl.lastWdtFeed = millis();

    config.setWdtPort(WdtPort{this, wdtFeedThunk, cooperateThunk});

    impl.ports.wdt = WdtPort{this, wdtFeedThunk, cooperateThunk};
    impl.ports.control = AppControlPort{
        this,
        requestRebootThunk,
        startProgramThunk,
        stopProgramThunk,
        startOtaUpdateThunk,
        factoryResetThunk,
        setThermostatThunk,
        setBatterySaverThunk,
        setInputRuntimeThunk,
        setInputTriggerThunk,
        setTempTriggerThunk,
    };
    logger.log("[Core] begin() done\n");
    impl.modeManager.init(webServer, impl.gsm, impl.mqtt, config, impl.otaHandler);
    impl.modeManager.switchMode(CoreMode::BOOT);
    return true;
}

void Core::updateEngineRunning() {
    CorePrivate& impl = *_impl;
    const auto& vcfg = config.getBase().vehicle;

    bool useInput = (strcmp(vcfg.engine_detection_source, "input") == 0);
    if (useInput) {
        const int8_t idx = impl.inputs.findIndexById(vcfg.engine_input_id);
        impl.engineRunning = (idx >= 0) ? impl.inputs.getState((uint8_t)idx) : false;
        return;
    }

    if (!impl.sensors.isVoltageValid()) return;
    float v = impl.sensors.getVoltage();
    float runTh = vcfg.engine_running_voltage_threshold;
    float stopTh = vcfg.engine_stopped_voltage_threshold;

    if (v >= runTh) impl.engineRunning = true;
    else if (v <= stopTh) impl.engineRunning = false;
}

void Core::serviceCellularLink() {
    CorePrivate& impl = *_impl;
    impl.cellular.service();
}

void Core::suspendCellularLink() {
    CorePrivate& impl = *_impl;
    impl.cellular.suspend();
}

bool Core::otaStreamFeed(const uint8_t* data, size_t len) {
    return _impl->otaHandler.streamFeed(data, len);
}

bool Core::otaStreamFinish() {
    return _impl->otaHandler.streamFinish();
}

void Core::otaStreamAbort() {
    _impl->otaHandler.streamAbort();
}

void Core::update() {
    CorePrivate& impl = *_impl;
    feedWatchdog();
    if (impl.pendingHardRestart) {
        core_hard_restart_now();
    }
    updateUptime();

    const uint32_t now = millis();

    if (getMode() == CoreMode::BOOT) {
        performPeriodicTasks(now);

        if (impl.programExecutor.isRunning()) {
            if (flashCommit.isPending()) {
                static uint32_t lastDeferredBusyLog = 0;
                if (now - lastDeferredBusyLog >= 2000UL) {
                    lastDeferredBusyLog = now;
                    logger.log("[Core] Deferred flash commit paused: program is running\n");
                }
            }
        }

        cooperate();

        if (impl.rebootRequested && (int32_t)(now - impl.rebootAtMs) >= 0) {
            impl.rebootRequested = false;
            reboot();
            return;
        }

        handleBoot();

        if (getMode() != CoreMode::BOOT) {
            webServer.update();
            drainDeferredOtaFromWebUpload_(impl);
            size_t modeIndex = static_cast<size_t>(impl.modeManager.getCurrentMode());
            if (modeIndex < sizeof(_modeHandlers) / sizeof(_modeHandlers[0]) && _modeHandlers[modeIndex]) {
                (this->*_modeHandlers[modeIndex])();
            }
        }
        return;
    }

    performPeriodicTasks(now);
    webServer.update();
    drainDeferredOtaFromWebUpload_(impl);

    if (impl.programExecutor.isRunning()) {
        if (flashCommit.isPending()) {
            static uint32_t lastDeferredBusyLog = 0;
            if (now - lastDeferredBusyLog >= 2000UL) {
                lastDeferredBusyLog = now;
                logger.log("[Core] Deferred flash commit paused: program is running\n");
            }
        }
    } else {
        flashCommit.tick(*this);
    }
    cooperate();

    if (impl.rebootRequested && (int32_t)(now - impl.rebootAtMs) >= 0) {
        impl.rebootRequested = false;
        reboot();
        return;
    }

    uint32_t start = micros();

    size_t modeIndex = static_cast<size_t>(impl.modeManager.getCurrentMode());
    if (modeIndex < sizeof(_modeHandlers) / sizeof(_modeHandlers[0]) && _modeHandlers[modeIndex]) {
        (this->*_modeHandlers[modeIndex])();
    }

    uint32_t elapsed = micros() - start;
    if (elapsed > impl.maxLoopTime) impl.maxLoopTime = elapsed;
    impl.loopCounter++;

    static uint32_t lastStatusBroadcast = 0;
    if (now - lastStatusBroadcast >= Timing::SSE_STATUS_INTERVAL_MS) {
        lastStatusBroadcast = now;
        sseBroadcastStatus();
    }
}

void Core::reboot() {
    logger.log("[Core] Rebooting...\n");
    // Не yield()/delay() здесь: ранний reboot после укороченного begin() ломает __yield().
    core_hard_restart_now();
}

void Core::requestReboot(uint32_t delayMs) {
    CorePrivate& impl = *_impl;
    impl.rebootRequested = true;
    impl.rebootAtMs = millis() + delayMs;
}

RelayController& Core::getRelay() { return _impl->relay; }
SensorsController& Core::getSensors() { return _impl->sensors; }
DigitalInputs& Core::getInputs() { return _impl->inputs; }
ProgramExecutor& Core::getProgramExecutor() { return _impl->programExecutor; }
GSMController& Core::getGSM() { return _impl->gsm; }
MQTTClient& Core::getMQTT() { return _impl->mqtt; }
ErrorManager& Core::getErrorManager() { return _impl->errorManager; }

