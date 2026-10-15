// src/core/TriggerManager.cpp
#include "core/TriggerManager.h"
#include "config/Config.h"
#include "common/Constants.h"
#include "common/Logger.h"

/**
 * @file TriggerManager.cpp
 * @brief Триггеры (входы и температура) → запуск программ с антидребезгом по фронту.
 *
 * Инварианты:
 * - `update()` time-gated (`Timing::TRIGGER_CHECK_INTERVAL_MS`) и не блокирует loop.
 * - Запуск программы — только на фронте условия (`trigger_rules`).
 * - Циклы ограничены `Limits::MAX_TRIGGERS`.
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
    memset(_tempEnabled, 0, sizeof(_tempEnabled));
}

void TriggerManager::syncEnabledFromConfig() {
    const auto& full = _config.getBase();
    const uint8_t nIn = (full.input_triggers_count < Limits::MAX_TRIGGERS)
                            ? full.input_triggers_count
                            : Limits::MAX_TRIGGERS;
    const uint8_t nTemp = (full.temperature_triggers_count < Limits::MAX_TRIGGERS)
                              ? full.temperature_triggers_count
                              : Limits::MAX_TRIGGERS;
    for (uint8_t i = 0; i < nIn; i++) {
        _inputEnabled[i] = full.input_triggers[i].enabled;
    }
    for (uint8_t i = nIn; i < Limits::MAX_TRIGGERS; i++) {
        _inputEnabled[i] = false;
    }
    for (uint8_t i = 0; i < nTemp; i++) {
        _tempEnabled[i] = full.temperature_triggers[i].enabled;
    }
    for (uint8_t i = nTemp; i < Limits::MAX_TRIGGERS; i++) {
        _tempEnabled[i] = false;
    }
}

void TriggerManager::begin() {
    logger.log(LogModule::TriggerManager, "begin\n");
    syncEnabledFromConfig();
    for (uint8_t i = 0; i < Limits::MAX_TRIGGERS; i++) {
        _inputLatch[i].reset();
        _tempLatch[i].reset();
    }
    const auto& full = _config.getBase();
    logger.log(LogModule::TriggerManager, "OK (%u input, %u temp)\n",
               full.input_triggers_count, full.temperature_triggers_count);
}

void TriggerManager::update() {
    uint32_t now = millis();
    const uint32_t interval =
        _pollIdle ? Timing::TRIGGER_CHECK_INTERVAL_IDLE_MS : Timing::TRIGGER_CHECK_INTERVAL_MS;
    if (now - _lastCheck < interval) return;
    _lastCheck = now;

    const auto& full = _config.getBase();
    const uint8_t nIn = (full.input_triggers_count < Limits::MAX_TRIGGERS)
                            ? full.input_triggers_count
                            : Limits::MAX_TRIGGERS;
    const uint8_t nTemp = (full.temperature_triggers_count < Limits::MAX_TRIGGERS)
                              ? full.temperature_triggers_count
                              : Limits::MAX_TRIGGERS;

    for (uint8_t i = 0; i < nIn; i++) {
        if (!_inputEnabled[i]) {
            _inputLatch[i].reset();
            continue;
        }
        const auto& t = full.input_triggers[i];
        const int8_t inIdx = _inputs.findIndexById(t.input_id);
        if (inIdx < 0) {
            _inputLatch[i].reset();
            continue;
        }
        if (!_inputs.isRuntimeEnabled((uint8_t)inIdx)) {
            _inputLatch[i].reset();
            continue;
        }
        const bool active = (_inputs.getState((uint8_t)inIdx) == (t.trigger_level == 1));
        if (_inputLatch[i].update(active) && t.program_id != 0) {
            logger.log(LogModule::TriggerManager, "Input trigger %d (input_id=%u idx=%d) activated, starting program %u\n",
                       i, (unsigned)t.input_id, (int)inIdx, t.program_id);
            _executor.start(t.program_id);
        }
    }
    for (uint8_t i = nIn; i < Limits::MAX_TRIGGERS; i++) {
        _inputLatch[i].reset();
    }

    for (uint8_t i = 0; i < nTemp; i++) {
        if (!_tempEnabled[i]) {
            _tempLatch[i].reset();
            continue;
        }
        const auto& t = full.temperature_triggers[i];
        const char* rom = "";
        for (uint8_t j = 0; j < HardwareLimits::SENSORS; j++) {
            if (_config.getBase().sensors[j].id == t.sensor_id) {
                rom = _config.getBase().sensors[j].rom;
                break;
            }
        }
        const int8_t idx = _sensors.findSensorIndexByRom(rom);
        if (idx < 0 || !_sensors.isTemperatureValid((uint8_t)idx)) {
            _tempLatch[i].reset();
            continue;
        }
        const float temp = _sensors.getTemperature((uint8_t)idx);
        const auto cmp = trigger_rules::compareFromString(t.comparison);
        if (_tempLatch[i].update(temp, t.threshold, cmp, DS18B20::RELEASE_HYSTERESIS_C) && t.program_id != 0) {
            logger.log(LogModule::TriggerManager, "Temp trigger %d activated, starting program %u\n", i, t.program_id);
            _executor.start(t.program_id);
        }
    }
    for (uint8_t i = nTemp; i < Limits::MAX_TRIGGERS; i++) {
        _tempLatch[i].reset();
    }
}

void TriggerManager::setInputTriggerEnabled(uint8_t index, bool en) {
    if (index < Limits::MAX_TRIGGERS) {
        _inputEnabled[index] = en;
        if (!en) _inputLatch[index].reset();
        logger.log(LogModule::TriggerManager, "Input trigger %u runtime enabled: %d\n", index, en);
    }
}

bool TriggerManager::isInputTriggerEnabled(uint8_t index) const {
    return (index < Limits::MAX_TRIGGERS) ? _inputEnabled[index] : false;
}

void TriggerManager::setTempTriggerEnabled(uint8_t index, bool en) {
    if (index < Limits::MAX_TRIGGERS) {
        _tempEnabled[index] = en;
        if (!en) _tempLatch[index].reset();
        logger.log(LogModule::TriggerManager, "Temp trigger %u runtime enabled: %d\n", index, en);
    }
}

bool TriggerManager::isTempTriggerEnabled(uint8_t index) const {
    return (index < Limits::MAX_TRIGGERS) ? _tempEnabled[index] : false;
}
