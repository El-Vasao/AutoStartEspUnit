// src/core/Core.Diagnostics.cpp
#include "core/Core.h"
#include "core/internal/CorePrivate.h"

#include "config/Config.h"
#include "fs/FSManager.h"
#include "common/Logger.h"
#include "common/Utils.h"
#include "common/EspHal.h"

#include <esp_system.h>

void Core::logHeapSnapshot(const char* tag) const {
    uint32_t freeHeap = espHalFreeHeap();
    uint32_t maxBlock = espHalMaxBlock();
    static uint32_t sessionMinFree = 0xFFFFFFFFu;
    static uint32_t sessionMinMaxBlk = 0xFFFFFFFFu;
    if (freeHeap < sessionMinFree) {
        sessionMinFree = freeHeap;
    }
    if (maxBlock < sessionMinMaxBlk) {
        sessionMinMaxBlk = maxBlock;
    }
    if (tag && tag[0]) {
        logger.log("[Core][%s] Heap: %u bytes, Max block: %u (since boot min free=%u min maxBlk=%u)\n", tag,
                   freeHeap, maxBlock, sessionMinFree, sessionMinMaxBlk);
    } else {
        logger.log("[Core] Heap: %u bytes, Max block: %u (since boot min free=%u min maxBlk=%u)\n",
                   freeHeap, maxBlock, sessionMinFree, sessionMinMaxBlk);
    }
}

void Core::performPeriodicTasks(uint32_t now) {
    CorePrivate& impl = *_impl;
    (void)now;

    // В BOOT FSM ещё пишет flash/парсит конфиг — не гоняем FS maintenance.
    if (getMode() == CoreMode::BOOT) {
        return;
    }

    if (every(Timing::FS_MAINTENANCE_INTERVAL_MS, impl.lastFsMaintenance)) {
        if (fileSystem.isInitialized()) {
            fileSystem.healthCheck();
            fileSystem.printStats();
        }
    }

    if (every(Timing::ERROR_REPORT_INTERVAL_MS, impl.lastErrorReport) && impl.errorManager.get() != ErrorCode::NONE) {
        logger.log("[Core] Active error: %s\n", impl.errorManager.getMessage());
    }

    if (every(Timing::ERROR_REPORT_INTERVAL_MS, impl.lastStatsPrint)) {
        logHeapSnapshot(nullptr);
        // ESP32-C3: heap frag auto-restart removed; snapshot is diagnostic only.
    }
}

const char* Core::getResetReason() const {
    static char reason[BufferBytes::Reset::REASON];
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   strlcpy(reason, "Power on", sizeof(reason)); break;
        case ESP_RST_EXT:       strlcpy(reason, "External", sizeof(reason)); break;
        case ESP_RST_SW:        strlcpy(reason, "Software", sizeof(reason)); break;
        case ESP_RST_PANIC:     strlcpy(reason, "Panic", sizeof(reason)); break;
        case ESP_RST_INT_WDT:   strlcpy(reason, "Interrupt WDT", sizeof(reason)); break;
        case ESP_RST_TASK_WDT:  strlcpy(reason, "Task WDT", sizeof(reason)); break;
        case ESP_RST_WDT:       strlcpy(reason, "Watchdog reset", sizeof(reason)); break;
        case ESP_RST_DEEPSLEEP: strlcpy(reason, "Deep sleep wake", sizeof(reason)); break;
        case ESP_RST_BROWNOUT:  strlcpy(reason, "Brownout", sizeof(reason)); break;
        case ESP_RST_SDIO:      strlcpy(reason, "SDIO", sizeof(reason)); break;
        default:                strlcpy(reason, "Unknown", sizeof(reason)); break;
    }
    return reason;
}

void Core::factoryReset() {
    logger.log("[Core] Factory reset - erasing config\n");
    if (fileSystem.isInitialized()) {
        fileSystem.deleteFile("/config.json");
    }
    reboot();
}

void Core::printStats() {
    const CorePrivate& impl = *_impl;
    logger.log("[Core] === STATS ===\n");
    logger.log("[Core] Mode: %s\n", getModeName());
    logger.log("[Core] Uptime: %u\n", getUptime());
    logger.log("[Core] Free Heap: %u\n", getFreeHeap());
    logger.log("[Core] Version: %s\n", getVersionString());
    logger.log("[Core] Last Error: %s\n", impl.errorManager.getMessage());
    logger.log("[Core] Error Time: %u\n", impl.errorManager.getTime());
    logger.log("[Core] ============\n");
}

bool Core::isRebootRequired() const {
    const CorePrivate& impl = *_impl;
    return impl.rebootRequired;
}

void Core::setRebootRequired(bool v) {
    CorePrivate& impl = *_impl;
    impl.rebootRequired = v;
}

bool Core::getThermostatRuntime() const {
    const CorePrivate& impl = *_impl;
    return impl.thermostatManager.isEnabled();
}

void Core::setThermostatRuntime(bool en) {
    CorePrivate& impl = *_impl;
    impl.thermostatManager.setEnabled(en);
}

bool Core::getBatterySaverRuntime() const {
    const CorePrivate& impl = *_impl;
    return impl.batterySaverManager.isEnabled();
}

void Core::setBatterySaverRuntime(bool en) {
    CorePrivate& impl = *_impl;
    impl.batterySaverManager.setEnabled(en);
}

bool Core::getTriggerRuntime(uint8_t index) const {
    const CorePrivate& impl = *_impl;
    return impl.triggerManager.isInputTriggerEnabled(index);
}

void Core::setTriggerRuntime(uint8_t index, bool en) {
    CorePrivate& impl = *_impl;
    impl.triggerManager.setInputTriggerEnabled(index, en);
}

bool Core::getTempTriggerRuntime(uint8_t index) const {
    const CorePrivate& impl = *_impl;
    return impl.triggerManager.isTempTriggerEnabled(index);
}

void Core::setTempTriggerRuntime(uint8_t index, bool en) {
    CorePrivate& impl = *_impl;
    impl.triggerManager.setTempTriggerEnabled(index, en);
}

bool Core::isEngineRunning() const {
    const CorePrivate& impl = *_impl;
    return impl.engineRunning;
}
