// src/core/Core.BatterySaverManager.cpp
#include "core/BatterySaverManager.h"
#include "config/Config.h"
#include "common/Utils.h"
#include "common/Logger.h"

/**
 * @file Core.BatterySaverManager.cpp
 * @brief Логика battery-saver (реакция на низкое напряжение, лимиты попыток/сутки).
 *
 * Инварианты:
 * - Полностью неблокирующий `update()`.
 * - Без heap/`String`.
 *
 * Запрещено:
 * - Выполнять долгие действия синхронно (всё через ProgramExecutor).
 */

BatterySaverManager::BatterySaverManager(Config& config, SensorsController& sensors, ProgramExecutor& executor) :
    _config(config),
    _sensors(sensors),
    _executor(executor),
    _runtimeEnabled(false),
    _lastAttempt(0),
    _attemptsToday(0),
    _dayStart(0),
    _lowStartTime(0)
{}

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

    if (_dayStart == 0) {
        _dayStart = now;
    }
    // “День” здесь условный: мы не используем RTC/реальные даты, поэтому сутки отсчитываются от первого старта.
    if (now - _dayStart >= Timing::MILLIS_PER_DAY) {
        _attemptsToday = 0;
        _dayStart = now;
    }

    float voltage = _sensors.getVoltage();
    if (!_sensors.isVoltageValid()) return;

    float startThreshold = bs.voltage_start_threshold;
    float abortThreshold = bs.voltage_abort_threshold;
    float hysteresis = bs.hysteresis;

    // Critical low: stop attempts and abort the battery-saver program if it is running.
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
        // Гистерезис защищает от дребезга вокруг порога: “низкое напряжение” должно закончиться уверенно.
        _lowStartTime = 0;
    }
}

