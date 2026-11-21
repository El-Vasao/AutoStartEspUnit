// src/core/Core.Modes.cpp
#include "core/Core.h"
#include "common/EspHal.h"
#include "core/internal/CorePrivate.h"

#include "config/Config.h"
#include "config/VehicleRules.h"
#include "io/HwMap.h"
#include "common/Logger.h"
#include "common/Pins.h"
#include "common/Utils.h"
#include "common/ErrorCodes.h"
#include "fs/FsManager.h"
#include "web/WebServer.h"

#include <WiFi.h>
#include "esp_system.h"

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
    CorePrivate& impl = *_impl;
    logHeapSnapshot("ota_start_requested");
    if (impl.modeManager.getCurrentMode() == CoreMode::OTA_UPDATE) {
        return;
    }
    if (impl.otaHandler.streamSucceeded()) {
        impl.modeManager.switchMode(CoreMode::OTA_UPDATE);
        return;
    }
    logger.log(LogModule::Core, "enter OTA skipped: stream via POST /upload\n");
}

void Core::onOtaHttpUploadStreamOpenedFromWeb() {
    CorePrivate& impl = *_impl;
    logHeapSnapshot("ota_upload_open");
    // Всегда (пере)запускать автомат потока до прихода кусков; отложенная смена режима не должна его сбрасывать.
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
    const CoreMode back = impl.otaReturnMode;
    impl.modeManager.switchMode(
        (back == CoreMode::SETUP_AP || back == CoreMode::EMERGENCY_AP || back == CoreMode::NORMAL)
            ? back
            : CoreMode::NORMAL);
}

void Core::onOtaHttpUploadAwaitTimedOut() {
    logHeapSnapshot("ota_upload_timeout");
    webServer.markOtaHttpUploadAwaitTimedOut();
    exitOtaToNormalMode();
}

bool Core::otaHoldsFlash() const {
    return _impl->otaHandler.holdsFlash();
}

void Core::handleBoot() {
    CorePrivate& impl = *_impl;

    // Ровно одна стадия за вызов: после тяжёлого `config.load()`/`reset()` в том же тике не гоняем
    // `ensureProgramIndex` + flash — иначе не успеваем покормить WDT (rst cause 4).
    switch (impl.bootStage) {
            case CorePrivate::BootStage::InitWiFiOff: {
                logger.log(LogModule::Core, "BOOT: Wi-Fi off\n");
                WiFi.persistent(false);
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                impl.bootStage = CorePrivate::BootStage::MountFS;
                break;
            }
            case CorePrivate::BootStage::MountFS: {
                logger.log(LogModule::Core, "BOOT: mounting FS\n");
                if (!fileSystem.begin()) {
                    impl.errorManager.set(ErrorCode::FS_MOUNT_FAIL);
                    logger.log(LogModule::Core, "FATAL: failed to mount LittleFS. Reboot on next tick...\n");
                    impl.bootStage = CorePrivate::BootStage::Done;
                    impl.pendingHardRestart = true;
                    espHalFeedWdt();
                    break;
                }
                impl.errorManager.loadFromRtc();
                {
                    // Любая нештатная перезагрузка → недоставленный факт для первого полного MQTT (см. docs/modules/mqtt.md).
                    const esp_reset_reason_t rr = esp_reset_reason();
                    ErrorCode bootCode = ErrorCode::NONE;
                    switch (rr) {
                        case ESP_RST_POWERON:
                        case ESP_RST_SW:
                        case ESP_RST_DEEPSLEEP:
                            break;
                        case ESP_RST_TASK_WDT:
                        case ESP_RST_INT_WDT:
                        case ESP_RST_WDT:
                            bootCode = ErrorCode::WDT_RESET;
                            break;
                        case ESP_RST_PANIC:
                            bootCode = ErrorCode::PANIC_RESET;
                            break;
                        case ESP_RST_BROWNOUT:
                            bootCode = ErrorCode::BROWNOUT_RESET;
                            break;
                        case ESP_RST_EXT:
                        case ESP_RST_SDIO:
                        case ESP_RST_UNKNOWN:
                        default:
                            bootCode = ErrorCode::UNEXPECTED_RESET;
                            break;
                    }
                    if (bootCode != ErrorCode::NONE) {
                        impl.errorManager.recordBootReset(bootCode);
                    }
                }
                impl.bootStage = CorePrivate::BootStage::LoadOrCreateConfig;
                break;
            }
            case CorePrivate::BootStage::LoadOrCreateConfig: {
                logger.log(LogModule::Core, "BOOT: loading or creating config\n");
                const ConfigLoadOutcome lo = config.loadWithOutcome();
                if (lo == ConfigLoadOutcome::OkAfterFactoryDefaultsWrittenRebootRecommended) {
                    // Сохранить причину сбоя загрузки до заводских значений (RTC переживает мягкую перезагрузку).
                    if (config.lastLoadError() != ErrorCode::NONE) {
                        impl.errorManager.set(config.lastLoadError());
                    }
                    logger.log(LogModule::Core, "factory defaults written; reboot on next tick\n");
                    impl.bootConfigLoaded = true;
                    impl.bootStage = CorePrivate::BootStage::Done;
                    impl.pendingHardRestart = true;
                    espHalFeedWdt();
                    break;
                }
                if (lo == ConfigLoadOutcome::Failed) {
                    const ErrorCode cfgErr = (config.lastLoadError() != ErrorCode::NONE)
                                                 ? config.lastLoadError()
                                                 : ErrorCode::CONFIG_MISSING;
                    impl.errorManager.set(cfgErr);
                    logger.log(LogModule::Core, "config load and factory reset failed. Enter EMERGENCY_AP.\n");
                    impl.bootTargetMode = CoreMode::EMERGENCY_AP;
                    impl.bootStage = CorePrivate::BootStage::SelectInitialMode;
                    break;
                }
                impl.bootConfigLoaded = true;
                if (const char* r = vehicle_rules::checkBlockInput(config.getBase())) {
                    logger.log(LogModule::Core, "start-lock input: %s — engine start stays locked\n", r);
                    impl.errorManager.set(ErrorCode::START_BLOCKED);
                }
                espHalFeedWdt();
                impl.bootStage = CorePrivate::BootStage::EnsureProgramIndex;
                break;
            }
            case CorePrivate::BootStage::EnsureProgramIndex: {
                logger.log(LogModule::Core, "BOOT: program index\n");
                if (impl.bootConfigLoaded) {
                    (void)config.ensureProgramIndex();
                }
                espHalFeedWdt();
                impl.bootStage = CorePrivate::BootStage::InitHardware;
                break;
            }
            case CorePrivate::BootStage::InitHardware: {
                logger.log(LogModule::Core, "BOOT: hardware init\n");
                // Карта каналов — порядок Pin::*PINS = шёлк K/IN.
                impl.sensors.begin();
                impl.relay.begin();
                impl.inputs.begin();

                impl.bootStage = CorePrivate::BootStage::InitManagers;
                break;
            }
            case CorePrivate::BootStage::InitManagers: {
                logger.log(LogModule::Core, "BOOT: manager init\n");
                impl.mqtt.setAppPorts(&impl.ports);
                impl.programExecutor.setLifecycleCallback(&MqttClient::onProgramLifecycleThunk, &impl.mqtt);
                impl.cellular.init(impl.gsm, impl.mqtt, webServer, impl.timeSyncManager);
                impl.triggerManager.begin();
                impl.batterySaverManager.begin();
                impl.thermostatManager.begin();
                impl.timeSyncManager.begin();
                impl.engineRunning = false;
                updateEngineRunning();
                impl.bootStage = CorePrivate::BootStage::SelectInitialMode;
                break;
            }
            case CorePrivate::BootStage::SelectInitialMode: {
                logger.log(LogModule::Core, "BOOT: selecting initial mode\n");
                CoreMode initialMode = CoreMode::NORMAL;
                if (impl.bootTargetMode == CoreMode::EMERGENCY_AP) {
                    initialMode = CoreMode::EMERGENCY_AP;
                } else if (impl.bootConfigLoaded && config.getBase().setup_required) {
                    initialMode = CoreMode::SETUP_AP;
                } else {
                    initialMode = CoreMode::NORMAL;
                }

                impl.modeManager.switchMode(initialMode);
                impl.bootStage = CorePrivate::BootStage::Done;
                logger.log(LogModule::Core, "boot complete, initial mode=%s\n", getModeName());
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
    impl.timeSyncManager.update();
    // SoftAP и сотовая связь сосуществуют на ESP32-C3. Та же политика сотовой связи, что в NORMAL_SILENT.
    serviceCellularLink();
    {
        uint8_t pid = 0;
        if (impl.gsm.takePendingProgramStart(pid)) {
            logger.log(LogModule::Core, "incoming DTMF, program %u start\n", (unsigned)pid);
            (void)impl.programExecutor.start(pid);
        }
    }

    const auto& wcfg = config.getBase().wifi;
    if (wcfg.ap_timeout_enabled && wcfg.ap_timeout_sec > 0 && impl.modeManager.getCurrentMode() == CoreMode::NORMAL) {
        if (webServer.isActive() && webServer.activeUiSessionCount() == 0) {
            const uint32_t idleMs = millis() - webServer.lastUiActivityMs();
            const uint32_t timeoutMs = secToMs((uint32_t)wcfg.ap_timeout_sec);
            if (idleMs >= timeoutMs) {
                logger.log(LogModule::Core, "AP timeout (%us), entering NORMAL_SILENT\n",
                           (unsigned)wcfg.ap_timeout_sec);
                impl.modeManager.switchMode(CoreMode::NORMAL_SILENT);
            }
        }
    }
}

void Core::handleNormalSilent() {
    CorePrivate& impl = *_impl;
    impl.inputs.update();
    // Кнопка платы (IN3): разбудить SoftAP независимо от включения триггера на этом входе.
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
    impl.timeSyncManager.update();
    serviceCellularLink();
    {
        uint8_t pid = 0;
        if (impl.gsm.takePendingProgramStart(pid)) {
            logger.log(LogModule::Core, "incoming DTMF, program %u start\n", (unsigned)pid);
            (void)impl.programExecutor.start(pid);
        }
    }
}

void Core::handleOTAUpdate() {
    CorePrivate& impl = *_impl;
    suspendCellularLink();
    impl.otaHandler.update();
}

