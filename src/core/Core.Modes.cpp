// src/core/Core.Modes.cpp
#include "core/Core.h"
#include "common/EspHal.h"
#include "core/internal/CorePrivate.h"
#include "core/internal/CoreBootHelpers.h"

#include "config/Config.h"
#include "common/Logger.h"
#include "common/Pins.h"
#include "fs/FSManager.h"
#include "web/WebServer.h"

#include <WiFi.h>

const char* Core::getModeName() const {
    const CorePrivate& impl = *_impl;
    if (impl.rebootRequired) {
        return StateStrings::MODE_REBOOT_REQUIRED;
    }
    return impl.modeManager.getModeName();
}

CoreMode Core::getMode() const {
    const CorePrivate& impl = *_impl;
    return impl.modeManager.getCurrentMode();
}

void Core::startOTAUpdate() {
    // Legacy /ota/start: stream OTA completes on upload final; keep as no-op enter if already streaming.
    CorePrivate& impl = *_impl;
    logHeapSnapshot("ota_start_requested");
    if (impl.modeManager.getCurrentMode() == CoreMode::OTA_UPDATE) {
        return;
    }
    if (impl.otaHandler.streamSucceeded()) {
        impl.modeManager.switchMode(CoreMode::OTA_UPDATE);
        return;
    }
    // No staged file path anymore — require an in-flight or completed stream session.
    logger.log("[Core] /ota/start ignored: use POST /upload stream OTA\n");
}

void Core::onOtaHttpUploadStreamOpenedFromWeb() {
    CorePrivate& impl = *_impl;
    logHeapSnapshot("ota_upload_open");
    // Always (re)start stream FSM before chunks arrive; deferred mode switch must not reset it.
    impl.otaHandler.prepareHttpUploadSession();
    if (impl.modeManager.getCurrentMode() != CoreMode::OTA_UPDATE) {
        impl.pendingDeferredOtaFromWebUpload = true;
    }
}

void Core::notifyOtaHttpUploadComplete(bool ok) {
    CorePrivate& impl = *_impl;
    impl.otaHandler.notifyHttpUploadComplete(ok);
    logHeapSnapshot(ok ? "ota_upload_ok" : "ota_upload_fail");
}

void Core::exitOtaToNormalMode() {
    CorePrivate& impl = *_impl;
    if (impl.modeManager.getCurrentMode() != CoreMode::OTA_UPDATE) return;
    impl.modeManager.switchMode(CoreMode::NORMAL);
}

void Core::onOtaHttpUploadAwaitTimedOut() {
    logHeapSnapshot("ota_upload_timeout");
    webServer.markOtaHttpUploadAwaitTimedOut();
    exitOtaToNormalMode();
}

void Core::handleBoot() {
    CorePrivate& impl = *_impl;

    // Ровно одна стадия за вызов: после тяжёлого `config.load()`/`reset()` в том же тике не гоняем
    // `ensureProgramIndex` + flash — иначе не успеваем покормить WDT (rst cause 4).
    switch (impl.bootStage) {
            case CorePrivate::BootStage::InitWiFiOff: {
                logger.log("[Core] BOOT: InitWiFiOff\n");
                WiFi.persistent(false);
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                impl.bootStage = CorePrivate::BootStage::MountFS;
                break;
            }
            case CorePrivate::BootStage::MountFS: {
                logger.log("[Core] BOOT: MountFS\n");
                if (!fileSystem.begin()) {
                    impl.errorManager.set(ErrorCode::FS_MOUNT_FAIL);
                    logger.log("[Core] FATAL: Cannot mount LittleFS. Will reboot on next tick...\n");
                    impl.bootStage = CorePrivate::BootStage::Done;
                    impl.pendingHardRestart = true;
                    espHalFeedWdt();
                    break;
                }
                impl.errorManager.loadFromRtc();
                impl.bootStage = CorePrivate::BootStage::LoadOrCreateConfig;
                break;
            }
            case CorePrivate::BootStage::LoadOrCreateConfig: {
                logger.log("[Core] BOOT: LoadOrCreateConfig\n");
                const ConfigLoadOutcome lo = config.loadWithOutcome();
                if (lo == ConfigLoadOutcome::OkAfterFactoryDefaultsWrittenRebootRecommended) {
                    logger.log("[Core] Factory defaults written; rebooting on next tick\n");
                    impl.bootConfigLoaded = true;
                    impl.bootStage = CorePrivate::BootStage::Done;
                    impl.pendingHardRestart = true;
                    espHalFeedWdt();
                    break;
                }
                if (lo == ConfigLoadOutcome::Failed) {
                    impl.errorManager.set(ErrorCode::CONFIG_MISSING);
                    logger.log("[Core] Config load and factory reset failed. Entering EMERGENCY_AP.\n");
                    impl.bootTargetMode = CoreMode::EMERGENCY_AP;
                    impl.bootStage = CorePrivate::BootStage::SelectInitialMode;
                    break;
                }
                impl.bootConfigLoaded = true;
                espHalFeedWdt();
                impl.bootStage = CorePrivate::BootStage::EnsureProgramIndex;
                break;
            }
            case CorePrivate::BootStage::EnsureProgramIndex: {
                logger.log("[Core] BOOT: EnsureProgramIndex\n");
                if (impl.bootConfigLoaded) {
                    (void)config.ensureProgramIndex();
                }
                espHalFeedWdt();
                impl.bootStage = CorePrivate::BootStage::InitHardware;
                break;
            }
            case CorePrivate::BootStage::InitHardware: {
                logger.log("[Core] BOOT: InitHardware\n");
                // Validate pin maps.
                const size_t inN = sizeof(Pin::INPUT_IDS) / sizeof(Pin::INPUT_IDS[0]);
                const size_t rlN = sizeof(Pin::RELAY_IDS) / sizeof(Pin::RELAY_IDS[0]);
                impl.bootHwMapBad = coreBoot_pinArrayHasZeroOrDuplicates(Pin::INPUT_IDS, inN) ||
                                    coreBoot_pinArrayHasZeroOrDuplicates(Pin::RELAY_IDS, rlN);
                if (impl.bootHwMapBad) {
                    impl.errorManager.set(ErrorCode::HW_MAP_INVALID);
                    logger.log("[Core] FATAL: Hardware map invalid (zero/duplicates in INPUT_IDS/RELAY_IDS)\n");
                }

                impl.sensors.begin();
                impl.relay.begin();
                if (impl.bootHwMapBad) impl.relay.allOff();
                impl.inputs.begin();

                impl.bootStage = CorePrivate::BootStage::InitManagers;
                break;
            }
            case CorePrivate::BootStage::InitManagers: {
                logger.log("[Core] BOOT: InitManagers\n");
                impl.mqtt.setAppPorts(&impl.ports);
                impl.programExecutor.setLifecycleCallback(&MQTTClient::onProgramLifecycleThunk, &impl.mqtt);
                impl.cellular.init(impl.gsm, impl.mqtt, webServer);
                impl.triggerManager.begin();
                impl.batterySaverManager.begin();
                impl.thermostatManager.begin();
                impl.engineRunning = false;
                updateEngineRunning();
                impl.bootStage = CorePrivate::BootStage::SelectInitialMode;
                break;
            }
            case CorePrivate::BootStage::SelectInitialMode: {
                logger.log("[Core] BOOT: SelectInitialMode\n");
                CoreMode initialMode = CoreMode::NORMAL;
                if (impl.bootTargetMode == CoreMode::EMERGENCY_AP) {
                    initialMode = CoreMode::EMERGENCY_AP;
                } else if (impl.bootHwMapBad) {
                    initialMode = CoreMode::EMERGENCY_AP;
                } else if (impl.bootConfigLoaded && config.getBase().setup_required) {
                    initialMode = CoreMode::SETUP_AP;
                } else {
                    initialMode = CoreMode::NORMAL;
                }

                impl.modeManager.switchMode(initialMode);
                impl.bootStage = CorePrivate::BootStage::Done;
                logger.log("[Core] Boot finished, initial mode=%s\n", getModeName());
                logHeapSnapshot("boot_done");
                break;
            }
            case CorePrivate::BootStage::Done:
            default:
                return;
    }
}

void Core::handleEmergencyAP() {
    suspendCellularLink();
}

void Core::handleSetupAP() {
    CorePrivate& impl = *_impl;
    suspendCellularLink();

    impl.inputs.update();
    impl.sensors.update();
    updateEngineRunning();
    impl.relay.update();
}

void Core::handleNormal() {
    CorePrivate& impl = *_impl;
    const bool pollIdle =
        !impl.programExecutor.isRunning() && webServer.activeUiSessionCount() == 0;
    impl.sensors.setPollIdle(pollIdle);
    impl.triggerManager.setPollIdle(pollIdle);

    impl.inputs.update();
    impl.sensors.update();
    updateEngineRunning();

    impl.programExecutor.update();
    impl.relay.update();
    impl.triggerManager.update();
    impl.batterySaverManager.update();
    impl.thermostatManager.update();
    // SoftAP + cellular coexist on ESP32-C3. Same cellular policy as NORMAL_SILENT.
    serviceCellularLink();

    const auto& wcfg = config.getBase().wifi;
    if (wcfg.ap_timeout_enabled && wcfg.ap_timeout_sec > 0 && impl.modeManager.getCurrentMode() == CoreMode::NORMAL) {
        if (webServer.isActive() && webServer.activeUiSessionCount() == 0) {
            const uint32_t idleMs = millis() - webServer.lastUiActivityMs();
            const uint32_t timeoutMs = (uint32_t)wcfg.ap_timeout_sec * 1000UL;
            if (idleMs >= timeoutMs) {
                logger.log("[Core] AP timeout reached (%us), switching to NORMAL_SILENT\n",
                           (unsigned)wcfg.ap_timeout_sec);
                impl.modeManager.switchMode(CoreMode::NORMAL_SILENT);
            }
        }
    }
}

void Core::handleNormalSilent() {
    CorePrivate& impl = *_impl;
    impl.inputs.update();
    // PCB button (IN3): wake SoftAP regardless of trigger enable on that input.
    if (impl.inputs.wasButtonPressed()) {
        (void)wakeWifiApFromSilent();
        return;
    }
    const bool pollIdle = !impl.programExecutor.isRunning();
    impl.sensors.setPollIdle(pollIdle);
    impl.triggerManager.setPollIdle(pollIdle);

    impl.sensors.update();
    updateEngineRunning();

    impl.programExecutor.update();
    impl.relay.update();
    impl.triggerManager.update();
    impl.batterySaverManager.update();
    impl.thermostatManager.update();
    serviceCellularLink();
}

void Core::handleOTAUpdate() {
    CorePrivate& impl = *_impl;
    suspendCellularLink();
    impl.otaHandler.update();
}

