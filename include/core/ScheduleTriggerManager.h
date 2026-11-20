// include/core/ScheduleTriggerManager.h
#pragma once

#include <Arduino.h>
#include "common/Constants.h"
#include "program/ProgramExecutor.h"

class Config;
class TimeSyncManager;

/**
 * Расписание локальных ЧЧ:ММ (+ days_mask) → старт программы по фронту один раз за местные сутки.
 * Срабатывает только пока TimeSyncManager::isSynced().
 */
class ScheduleTriggerManager {
public:
    ScheduleTriggerManager(Config& config, TimeSyncManager& timeSync, ProgramExecutor& executor);

    void begin();
    void update();

    /// Копирует флаги включения из конфига, не очищая ключи дня последнего срабатывания (восстановление после OTA).
    void syncEnabledFromConfig();

    void setEnabled(uint8_t index, bool en);
    bool isEnabled(uint8_t index) const;

private:
    Config& _config;
    TimeSyncManager& _timeSync;
    ProgramExecutor& _executor;

    bool _enabled[Limits::MAX_SCHEDULE_TRIGGERS]{};
    /// Упакованная местная дата последнего срабатывания: (year<<9)|(yday) — достаточно, чтобы заметить новые местные сутки.
    uint32_t _lastFireDayKey[Limits::MAX_SCHEDULE_TRIGGERS]{};
    uint32_t _lastCheck{0};
};
