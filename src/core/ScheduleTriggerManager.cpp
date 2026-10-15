// src/core/ScheduleTriggerManager.cpp
#include "core/ScheduleTriggerManager.h"

#include "config/Config.h"
#include "core/TimeSyncManager.h"
#include "core/TriggerRules.h"
#include "common/Logger.h"
#include "common/Utils.h"

#include <string.h>
#include <time.h>

/**
 * @file ScheduleTriggerManager.cpp
 * @brief Local schedule triggers (HH:MM + weekday mask).
 * Marks the local day only after ProgramExecutor::start() succeeds.
 */

ScheduleTriggerManager::ScheduleTriggerManager(Config& config, TimeSyncManager& timeSync,
                                               ProgramExecutor& executor)
    : _config(config), _timeSync(timeSync), _executor(executor), _lastCheck(0) {
    memset(_enabled, 0, sizeof(_enabled));
    memset(_lastFireDayKey, 0, sizeof(_lastFireDayKey));
}

void ScheduleTriggerManager::syncEnabledFromConfig() {
    const auto& full = _config.getBase();
    const uint8_t n = (full.schedule_triggers_count < Limits::MAX_SCHEDULE_TRIGGERS)
                          ? full.schedule_triggers_count
                          : Limits::MAX_SCHEDULE_TRIGGERS;
    for (uint8_t i = 0; i < n; i++) {
        _enabled[i] = full.schedule_triggers[i].enabled;
    }
    for (uint8_t i = n; i < Limits::MAX_SCHEDULE_TRIGGERS; i++) {
        _enabled[i] = false;
    }
}

void ScheduleTriggerManager::begin() {
    logger.log(LogModule::ScheduleTriggerManager, "begin\n");
    syncEnabledFromConfig();
    memset(_lastFireDayKey, 0, sizeof(_lastFireDayKey));
    logger.log(LogModule::ScheduleTriggerManager, "OK (%u)\n", _config.getBase().schedule_triggers_count);
}

void ScheduleTriggerManager::update() {
    const uint32_t now = millis();
    if (now - _lastCheck < Timing::TRIGGER_CHECK_INTERVAL_MS) return;
    _lastCheck = now;

    if (!_timeSync.isSynced()) return;

    struct tm local {};
    if (!_timeSync.localBrokenDown(local)) return;

    const uint32_t todayKey = calendarDayKey(local);
    const auto& full = _config.getBase();

    for (uint8_t i = 0; i < full.schedule_triggers_count && i < Limits::MAX_SCHEDULE_TRIGGERS; i++) {
        if (!_enabled[i]) continue;
        const auto& s = full.schedule_triggers[i];
        if (s.program_id == 0) continue;
        const bool due = trigger_rules::scheduleDue(s.hour, s.minute, s.days_mask,
                                                    local.tm_hour, local.tm_min, local.tm_wday);
        if (!trigger_rules::scheduleShouldFire(due, todayKey, _lastFireDayKey[i])) continue;

        logger.log(LogModule::ScheduleTriggerManager, "#%u %02u:%02u -> program %u\n", (unsigned)i, (unsigned)s.hour,
                   (unsigned)s.minute, (unsigned)s.program_id);
        if (_executor.start(s.program_id)) {
            _lastFireDayKey[i] = todayKey;
        }
    }
}

void ScheduleTriggerManager::setEnabled(uint8_t index, bool en) {
    if (index < Limits::MAX_SCHEDULE_TRIGGERS) {
        _enabled[index] = en;
        logger.log(LogModule::ScheduleTriggerManager, "%u runtime enabled: %d\n", index, en);
    }
}

bool ScheduleTriggerManager::isEnabled(uint8_t index) const {
    return (index < Limits::MAX_SCHEDULE_TRIGGERS) ? _enabled[index] : false;
}
