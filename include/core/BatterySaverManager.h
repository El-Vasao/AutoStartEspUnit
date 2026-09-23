// include/core/BatterySaverManager.h
#pragma once

#include <Arduino.h>
#include "io/SensorsController.h"
#include "program/ProgramExecutor.h"

class Config;
class TimeSyncManager;

/**
 * @brief Менеджер функции Battery Saver.
 * Отслеживает напряжение и запускает программу при длительном низком напряжении.
 *
 * Защиты:
 * - `voltage_abort_threshold`: немедленный abort попыток и stop программы battery saver;
 * - минимальная длительность низкого напряжения до старта;
 * - минимальный интервал между попытками;
 * - лимит попыток в сутки (календарные сутки при synced wall-clock; иначе псевдо-день от millis).
 */
class BatterySaverManager {
public:
    BatterySaverManager(Config& config, SensorsController& sensors, ProgramExecutor& executor,
                        TimeSyncManager& timeSync);

    void begin();
    void update();

    void setEnabled(bool en) { _runtimeEnabled = en; }
    bool isEnabled() const { return _runtimeEnabled; }

private:
    Config& _config;
    SensorsController& _sensors;
    ProgramExecutor& _executor;
    TimeSyncManager& _timeSync;

    bool _runtimeEnabled;
    uint32_t _lastAttempt;
    uint8_t _attemptsToday;
    uint32_t _dayStart;
    uint32_t _calendarDayKey;
    uint32_t _lowStartTime;
};
