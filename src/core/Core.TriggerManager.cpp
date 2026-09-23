// src/core/Core.TriggerManager.cpp
#include "core/TriggerManager.h"
#include "config/Config.h"
#include "common/Constants.h"
#include "common/Logger.h"

/**
 * @file Core.TriggerManager.cpp
 * @brief Триггеры (входы и температура) → запуск программ с антидребезгом по фронту.
 *
 * Инварианты:
 * - `update()` time-gated (`Timing::TRIGGER_CHECK_INTERVAL_MS`) и не блокирует loop.
 * - Запуск программы — только на фронте условия.
 *
 * Память:
 * - Только фиксированные массивы (по `Limits::MAX_TRIGGERS`), без heap/`String`.
 */

TriggerManager::TriggerManager(Config& config, DigitalInputs& inputs, SensorsController& sensors, ProgramExecutor& executor) :
    _config(config),
    _inputs(inputs),
    _sensors(sensors),
    _executor(executor),
    _lastCheck(0)
{
    memset(_inputEnabled, 0, sizeof(_inputEnabled));
    memset(_inputLastState, 0, sizeof(_inputLastState));
    memset(_tempEnabled, 0, sizeof(_tempEnabled));
    memset(_tempLastState, 0, sizeof(_tempLastState));
}

void TriggerManager::begin() {
    logger.log("[TriggerManager] begin\n");
    const auto& full = _config.getBase();

    for (uint8_t i = 0; i < full.input_triggers_count; i++) {
        _inputEnabled[i] = full.input_triggers[i].enabled;
        _inputLastState[i] = false;
    }

    for (uint8_t i = 0; i < full.temperature_triggers_count; i++) {
        _tempEnabled[i] = full.temperature_triggers[i].enabled;
        _tempLastState[i] = false;
    }

    logger.log("[TriggerManager] OK (%u input, %u temp)\n",
               full.input_triggers_count, full.temperature_triggers_count);
}

void TriggerManager::update() {
    uint32_t now = millis();
    const uint32_t interval =
        _pollIdle ? Timing::TRIGGER_CHECK_INTERVAL_IDLE_MS : Timing::TRIGGER_CHECK_INTERVAL_MS;
    if (now - _lastCheck < interval) return;
    _lastCheck = now;

    const auto& full = _config.getBase();

    for (uint8_t i = 0; i < full.input_triggers_count; i++) {
        if (!_inputEnabled[i]) {
            _inputLastState[i] = false;
            continue;
        }
        const auto& t = full.input_triggers[i];
        const int8_t inIdx = _inputs.findIndexById(t.input_id);
        if (inIdx < 0) {
            _inputLastState[i] = false;
            continue;
        }
        // Проверяем, включён ли вход в рантайме
        if (!_inputs.isRuntimeEnabled((uint8_t)inIdx)) {
            _inputLastState[i] = false;
            continue;
        }
        // Триггерный уровень хранится как 0/1. getState() возвращает bool (активен/не активен),
        // поэтому сравниваем с (trigger_level == 1).
        bool active = (_inputs.getState((uint8_t)inIdx) == (t.trigger_level == 1));
        if (active && t.program_id != 0) {
            if (!_inputLastState[i]) {
                logger.log("[TriggerManager] Input trigger %d (input_id=%u idx=%d) activated, starting program %u\n",
                           i, (unsigned)t.input_id, (int)inIdx, t.program_id);
                _executor.start(t.program_id);
                _inputLastState[i] = true;
            }
        } else {
            _inputLastState[i] = false;
        }
    }

    for (uint8_t i = 0; i < full.temperature_triggers_count; i++) {
        if (!_tempEnabled[i]) {
            _tempLastState[i] = false;
            continue;
        }
        const auto& t = full.temperature_triggers[i];
        const char* rom = "";
        for (uint8_t j = 0; j < HardwareLimits::SENSORS; j++) {
            if (config.getBase().sensors[j].id == t.sensor_id) {
                rom = config.getBase().sensors[j].rom;
                break;
            }
        }
        const int8_t idx = _sensors.findSensorIndexByRom(rom);
        if (idx < 0) continue;
        float temp = _sensors.getTemperature((uint8_t)idx);
        if (!_sensors.isTemperatureValid((uint8_t)idx)) continue;

        bool condition = false;
        if (strcmp(t.comparison, "above") == 0) {
            condition = (temp > t.threshold);
        } else if (strcmp(t.comparison, "below") == 0) {
            condition = (temp < t.threshold);
        }

        if (condition && t.program_id != 0) {
            if (!_tempLastState[i]) {
                logger.log("[TriggerManager] Temp trigger %d activated, starting program %u\n", i, t.program_id);
                _executor.start(t.program_id);
                _tempLastState[i] = true;
            }
        } else {
            _tempLastState[i] = false;
        }
    }
}

void TriggerManager::setInputTriggerEnabled(uint8_t index, bool en) {
    if (index < Limits::MAX_TRIGGERS) {
        _inputEnabled[index] = en;
        logger.log("[TriggerManager] Input trigger %u runtime enabled: %d\n", index, en);
    }
}

bool TriggerManager::isInputTriggerEnabled(uint8_t index) const {
    return (index < Limits::MAX_TRIGGERS) ? _inputEnabled[index] : false;
}

void TriggerManager::setTempTriggerEnabled(uint8_t index, bool en) {
    if (index < Limits::MAX_TRIGGERS) {
        _tempEnabled[index] = en;
        logger.log("[TriggerManager] Temp trigger %u runtime enabled: %d\n", index, en);
    }
}

bool TriggerManager::isTempTriggerEnabled(uint8_t index) const {
    return (index < Limits::MAX_TRIGGERS) ? _tempEnabled[index] : false;
}

