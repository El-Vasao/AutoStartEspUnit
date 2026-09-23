// src/core/Core.ScheduleTriggerManager.cpp
#include "core/ScheduleTriggerManager.h"

#include "config/Config.h"
#include "core/TimeSyncManager.h"
#include "common/Logger.h"

#include <string.h>
#include <time.h>

/**
 * @file Core.ScheduleTriggerManager.cpp
 * @brief Local schedule triggers (HH:MM + weekday mask).
 */

namespace {
uint32_t dayKeyFromTm(const struct tm& t) {
    // tm_year since 1900, tm_yday 0..365
    return (static_cast<uint32_t>(t.tm_year & 0x1FF) << 9) | static_cast<uint32_t>(t.tm_yday & 0x1FF);
}

/// Convert tm_wday (0=Sun) to schedule bit (bit0=Mon … bit6=Sun).
uint8_t scheduleBitFromTmWday(int tmWday) {
    if (tmWday < 0 || tmWday > 6) return 0;
    if (tmWday == 0) return (1u << 6); // Sunday
    return static_cast<uint8_t>(1u << (tmWday - 1));
}
} // namespace

ScheduleTriggerManager::ScheduleTriggerManager(Config& config, TimeSyncManager& timeSync,
                                               ProgramExecutor& executor)
    : _config(config), _timeSync(timeSync), _executor(executor), _lastCheck(0) {
    memset(_enabled, 0, sizeof(_enabled));
    memset(_lastFireDayKey, 0, sizeof(_lastFireDayKey));
}

void ScheduleTriggerManager::begin() {
    logger.log("[ScheduleTrigger] begin\n");
    const auto& full = _config.getBase();
    for (uint8_t i = 0; i < full.schedule_triggers_count && i < Limits::MAX_SCHEDULE_TRIGGERS; i++) {
        _enabled[i] = full.schedule_triggers[i].enabled;
        _lastFireDayKey[i] = 0;
    }
    for (uint8_t i = full.schedule_triggers_count; i < Limits::MAX_SCHEDULE_TRIGGERS; i++) {
        _enabled[i] = false;
        _lastFireDayKey[i] = 0;
    }
    logger.log("[ScheduleTrigger] OK (%u)\n", full.schedule_triggers_count);
}

void ScheduleTriggerManager::update() {
    const uint32_t now = millis();
    if (now - _lastCheck < Timing::TRIGGER_CHECK_INTERVAL_MS) return;
    _lastCheck = now;

    if (!_timeSync.isSynced()) return;

    struct tm local {};
    if (!_timeSync.localBrokenDown(local)) return;

    const uint32_t todayKey = dayKeyFromTm(local);
    const uint8_t dowBit = scheduleBitFromTmWday(local.tm_wday);
    const auto& full = _config.getBase();

    for (uint8_t i = 0; i < full.schedule_triggers_count && i < Limits::MAX_SCHEDULE_TRIGGERS; i++) {
        if (!_enabled[i]) continue;
        const auto& s = full.schedule_triggers[i];
        if (s.program_id == 0) continue;
        if (s.hour > 23 || s.minute > 59) continue;
        const uint8_t mask = s.days_mask ? s.days_mask : 0x7F;
        if ((mask & dowBit) == 0) continue;
        if (local.tm_hour != s.hour || local.tm_min != s.minute) continue;
        if (_lastFireDayKey[i] == todayKey) continue;

        logger.log("[ScheduleTrigger] #%u %02u:%02u -> program %u\n", (unsigned)i, (unsigned)s.hour,
                   (unsigned)s.minute, (unsigned)s.program_id);
        _executor.start(s.program_id);
        _lastFireDayKey[i] = todayKey;
    }
}

void ScheduleTriggerManager::setEnabled(uint8_t index, bool en) {
    if (index < Limits::MAX_SCHEDULE_TRIGGERS) {
        _enabled[index] = en;
        logger.log("[ScheduleTrigger] %u runtime enabled: %d\n", index, en);
    }
}

bool ScheduleTriggerManager::isEnabled(uint8_t index) const {
    return (index < Limits::MAX_SCHEDULE_TRIGGERS) ? _enabled[index] : false;
}
