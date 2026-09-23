// src/core/Core.BatterySaverManager.cpp
#include "core/BatterySaverManager.h"
#include "core/TimeSyncManager.h"
#include "config/Config.h"
#include "common/Utils.h"
#include "common/Logger.h"

#include <time.h>

/**
 * @file Core.BatterySaverManager.cpp
 * @brief Логика battery-saver (реакция на низкое напряжение, лимиты попыток/сутки).
 */

namespace {
uint32_t calendarDayKey(const struct tm& t) {
    return (static_cast<uint32_t>(t.tm_year & 0x1FF) << 9) | static_cast<uint32_t>(t.tm_yday & 0x1FF);
}
} // namespace

BatterySaverManager::BatterySaverManager(Config& config, SensorsController& sensors, ProgramExecutor& executor,
                                         TimeSyncManager& timeSync)
    : _config(config),
      _sensors(sensors),
      _executor(executor),
      _timeSync(timeSync),
      _runtimeEnabled(false),
      _lastAttempt(0),
      _attemptsToday(0),
      _dayStart(0),
      _calendarDayKey(0),
      _lowStartTime(0) {}

void BatterySaverManager::begin() {
    logger.log("[BatterySaverManager] begin\n");
    _runtimeEnabled = _config.getBase().battery_saver.enabled;
    logger.log("[BatterySaverManager] Runtime enabled: %d\n", _runtimeEnabled);
}

void BatterySaverManager::update() {
    if (!_runtimeEnabled) {
        _lowStartTime = 0;
        return;
    }

    const auto& bs = _config.getBase().battery_saver;
    if (!bs.enabled) return;

    uint32_t now = millis();

    struct tm local {};
    if (_timeSync.isSynced() && _timeSync.localBrokenDown(local)) {
        const uint32_t key = calendarDayKey(local);
        if (_calendarDayKey == 0) {
            _calendarDayKey = key;
        } else if (key != _calendarDayKey) {
            _attemptsToday = 0;
            _calendarDayKey = key;
            _dayStart = now;
        }
    } else {
        if (_dayStart == 0) {
            _dayStart = now;
        }
        if (now - _dayStart >= Timing::MILLIS_PER_DAY) {
            _attemptsToday = 0;
            _dayStart = now;
            _calendarDayKey = 0;
        }
    }

    float voltage = _sensors.getVoltage();
    if (!_sensors.isVoltageValid()) return;

    float startThreshold = bs.voltage_start_threshold;
    float abortThreshold = bs.voltage_abort_threshold;
    float hysteresis = bs.hysteresis;

    if (voltage < abortThreshold) {
        _lowStartTime = 0;
        if (bs.program_id != 0 && _executor.isRunning() &&
            _executor.getCurrentProgramId() == bs.program_id) {
            logger.log("[BatterySaverManager] Abort program %u: voltage %.2f < abort %.2f\n",
                       bs.program_id, voltage, abortThreshold);
            _executor.stop();
        }
        return;
    }

    if (voltage < startThreshold) {
        if (_lowStartTime == 0) {
            _lowStartTime = now;
        }

        uint32_t duration = now - _lowStartTime;
        uint32_t required = secToMs(bs.min_low_voltage_duration_sec);

        if (duration >= required) {
            if (_attemptsToday >= bs.max_attempts_per_day) return;
            if (now - _lastAttempt < secToMs(bs.min_time_between_attempts_sec)) return;

            if (bs.program_id != 0) {
                logger.log("[BatterySaverManager] Starting program %u due to low voltage\n", bs.program_id);
                _executor.start(bs.program_id);
                _attemptsToday++;
                _lastAttempt = now;
            }

            _lowStartTime = 0;
        }
    } else if (voltage > startThreshold + hysteresis) {
        _lowStartTime = 0;
    }
}
