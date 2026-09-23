// include/core/ScheduleTriggerManager.h
#pragma once

#include <Arduino.h>
#include "common/Constants.h"
#include "program/ProgramExecutor.h"

class Config;
class TimeSyncManager;

/**
 * Local HH:MM (+ days_mask) schedule → program start on rising edge once per local day.
 * Fires only while TimeSyncManager::isSynced().
 */
class ScheduleTriggerManager {
public:
    ScheduleTriggerManager(Config& config, TimeSyncManager& timeSync, ProgramExecutor& executor);

    void begin();
    void update();

    void setEnabled(uint8_t index, bool en);
    bool isEnabled(uint8_t index) const;

private:
    Config& _config;
    TimeSyncManager& _timeSync;
    ProgramExecutor& _executor;

    bool _enabled[Limits::MAX_SCHEDULE_TRIGGERS]{};
    /// Packed local date of last fire: (year<<9)|(yday) — enough to detect new local day.
    uint32_t _lastFireDayKey[Limits::MAX_SCHEDULE_TRIGGERS]{};
    uint32_t _lastCheck{0};
};
