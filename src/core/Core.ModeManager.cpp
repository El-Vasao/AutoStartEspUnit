// src/core/Core.ModeManager.cpp
#include "core/ModeManager.h"
#include "web/WebServer.h"
#include "gsm/GSMController.h"
#include "mqtt/MQTTClient.h"
#include "config/Config.h"
#include "ota/OTAHandler.h"
#include "common/Pins.h"
#include "core/Core.h"
#include "io/RelayController.h"
#include "program/ProgramExecutor.h"
#include "common/Version.h"
#include "common/Logger.h"

/**
 * @file Core.ModeManager.cpp
 * @brief Менеджер режимов ядра (BOOT/EMERGENCY_AP/SETUP_AP/NORMAL/NORMAL_SILENT/OTA_UPDATE).
 *
 * Ответственность:
 * - Координировать переходы режимов и включение/выключение подсистем (web/gsm/mqtt/ota).
 *
 * Инварианты (ESP8266):
 * - Переходы должны быть быстрыми (без долгих блокировок), чтобы не душить WiFi/lwIP и watchdog.
 * - Не аллоцировать heap в hot-path.
 *
 * Запрещено:
 * - Делать тяжёлые операции файловой системы/OTA в enter/exit без явного time-slicing.
 */

ModeManager::ModeManager() :
    _currentMode(CoreMode::BOOT),
    _modeEnterTime(0),
    _webServer(nullptr),
    _gsm(nullptr),
    _mqtt(nullptr),
    _config(nullptr),
    _ota(nullptr)
{}

void ModeManager::init(WebServer& webServer, GSMController& gsm, MQTTClient& mqtt,
                       Config& config, OTAHandler& ota) {
    _webServer = &webServer;
    _gsm = &gsm;
    _mqtt = &mqtt;
    _config = &config;
    _ota = &ota;
}

void ModeManager::switchMode(CoreMode newMode) {
    if (_currentMode == newMode) return;

    const char* oldModeName = getModeName();
    const char* newModeName = "unknown";
    switch (newMode) {
        case CoreMode::BOOT:            newModeName = StateStrings::MODE_BOOT; break;
        case CoreMode::EMERGENCY_AP:    newModeName = StateStrings::MODE_EMERGENCY; break;
        case CoreMode::SETUP_AP:        newModeName = StateStrings::MODE_SETUP; break;
        case CoreMode::NORMAL:          newModeName = StateStrings::MODE_NORMAL; break;
        case CoreMode::NORMAL_SILENT:   newModeName = StateStrings::MODE_NORMAL_SILENT; break;
        case CoreMode::OTA_UPDATE:      newModeName = StateStrings::MODE_OTA; break;
        default: break;
    }

    logger.log("[ModeManager] Switching mode: %s -> %s\n", oldModeName, newModeName);

    // Выходим из текущего режима.
    // Важно: exit/enter должны быть быстрыми и не делать тяжёлых блокирующих операций.
    switch (_currentMode) {
        case CoreMode::BOOT:            exitBoot(); break;
        case CoreMode::EMERGENCY_AP:    exitEmergencyAP(); break;
        case CoreMode::SETUP_AP:        exitSetupAP(); break;
        case CoreMode::NORMAL:          exitNormal(); break;
        case CoreMode::NORMAL_SILENT:   exitNormalSilent(); break;
        case CoreMode::OTA_UPDATE:      exitOTAUpdate(); break;
        default: break;
    }

    _currentMode = newMode;
    _modeEnterTime = millis();

    // Входим в новый режим.
    switch (_currentMode) {
        case CoreMode::BOOT:            enterBoot(); break;
        case CoreMode::EMERGENCY_AP:    enterEmergencyAP(); break;
        case CoreMode::SETUP_AP:        enterSetupAP(); break;
        case CoreMode::NORMAL:          enterNormal(); break;
        case CoreMode::NORMAL_SILENT:   enterNormalSilent(); break;
        case CoreMode::OTA_UPDATE:      enterOTAUpdate(); break;
        default: break;
    }

    // Сразу отправляем статус: UI должен увидеть смену режима без ожидания периодического тика.
    // Шлём “принудительный” статус, чтобы он не потерялся, если очередь логов занята (например, во время OTA).
    if (_webServer) {
        _webServer->broadcastStatusForce();
    }
}

const char* ModeManager::getModeName() const {
    switch (_currentMode) {
        case CoreMode::BOOT:            return StateStrings::MODE_BOOT;
        case CoreMode::EMERGENCY_AP:    return StateStrings::MODE_EMERGENCY;
        case CoreMode::SETUP_AP:        return StateStrings::MODE_SETUP;
        case CoreMode::NORMAL:          return StateStrings::MODE_NORMAL;
        case CoreMode::NORMAL_SILENT:   return StateStrings::MODE_NORMAL_SILENT;
        case CoreMode::OTA_UPDATE:      return StateStrings::MODE_OTA;
        default:                        return "unknown";
    }
}

// --- Методы входа/выхода ---

void ModeManager::enterBoot() {
    logger.log("[ModeManager] enterBoot\n");
}

void ModeManager::exitBoot() {}

void ModeManager::enterEmergencyAP() {
    logger.log("[ModeManager] enterEmergencyAP\n");
    if (_webServer) {
        _webServer->start(CoreMode::EMERGENCY_AP);
    }
}

void ModeManager::exitEmergencyAP() {
    logger.log("[ModeManager] exitEmergencyAP\n");
    if (_webServer) {
        _webServer->stop();
    }
}

void ModeManager::enterSetupAP() {
    logger.log("[ModeManager] enterSetupAP\n");
    if (_webServer) {
        _webServer->start(CoreMode::SETUP_AP);
    }
}

void ModeManager::exitSetupAP() {
    logger.log("[ModeManager] exitSetupAP\n");
    if (_webServer) {
        _webServer->stop();
    }
}

void ModeManager::enterNormal() {
    logger.log("[ModeManager] enterNormal\n");
    if (_webServer) {
        _webServer->start(CoreMode::NORMAL);
    }
    // GSM/MQTT lifecycle обслуживается в Core::handleNormal/Core::handleNormalSilent.
}

void ModeManager::exitNormal() {
    logger.log("[ModeManager] exitNormal\n");
}

void ModeManager::enterNormalSilent() {
    logger.log("[ModeManager] enterNormalSilent\n");
    if (_webServer) {
        _webServer->stop();
    }
}

void ModeManager::exitNormalSilent() {
    logger.log("[ModeManager] exitNormalSilent\n");
}

void ModeManager::enterOTAUpdate() {
    logger.log("[ModeManager] enterOTAUpdate\n");
    // Unload domain work; keep SSE open for UI progress/logs during stream flash.
    core.logHeapSnapshot("mode_enter_ota");
    if (core.getProgramExecutor().isRunning()) {
        core.getProgramExecutor().stop();
    }
    core.getRelay().allOff();
    core.suspendDomainManagersForOta();
    if (_ota) {
        _ota->begin();
    }
}

void ModeManager::exitOTAUpdate() {
    logger.log("[ModeManager] exitOTAUpdate\n");
    core.logHeapSnapshot("mode_exit_ota");
    if (_ota) {
        _ota->onModeExit();
    }
    core.restoreDomainManagersAfterOta();
}

