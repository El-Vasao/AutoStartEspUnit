// src/core/ThermostatManager.cpp
#include "core/ThermostatManager.h"
#include "config/Config.h"
#include "common/Constants.h"
#include "common/Logger.h"

/**
 * @file ThermostatManager.cpp
 * @brief Логика термостата (пороговые условия → запуск программ).
 *
 * Инварианты:
 * - Неблокирующий `update()`.
 * - Запуск программ только по фронту с гистерезисом отпускания (`DS18B20::RELEASE_HYSTERESIS_C`).
 *
 * Память:
 * - Без heap/`String`.
 */

ThermostatManager::ThermostatManager(Config& config, SensorsController& sensors, ProgramExecutor& executor) :
    _config(config),
    _sensors(sensors),
    _executor(executor),
    _runtimeEnabled(false)
{}

void ThermostatManager::begin() {
    logger.log(LogModule::ThermostatManager, "begin\n");
    _runtimeEnabled = _config.getBase().thermostat.enabled;
    logger.log(LogModule::ThermostatManager, "Runtime enabled: %d\n", _runtimeEnabled);
}

void ThermostatManager::update() {
    const auto& ts = _config.getBase().thermostat;
    if (!_runtimeEnabled || !ts.enabled) {
        _lowerLatch.reset();
        _upperLatch.reset();
        return;
    }

    const char* rom = "";
    for (uint8_t j = 0; j < HardwareLimits::SENSORS; j++) {
        if (_config.getBase().sensors[j].id == ts.sensor_id) {
            rom = _config.getBase().sensors[j].rom;
            break;
        }
    }
    const int8_t idx = _sensors.findSensorIndexByRom(rom);
    if (idx < 0 || !_sensors.isTemperatureValid((uint8_t)idx)) {
        _lowerLatch.reset();
        _upperLatch.reset();
        return;
    }
    const float temp = _sensors.getTemperature((uint8_t)idx);
    const auto cmp = trigger_rules::compareFromString(ts.comparison);
    const float hyst = DS18B20::RELEASE_HYSTERESIS_C;

    if (_lowerLatch.update(temp, ts.lower_threshold, cmp, hyst) && ts.program_id_lower != 0) {
        logger.log(LogModule::ThermostatManager, "Lower threshold reached, starting program %u\n", ts.program_id_lower);
        _executor.start(ts.program_id_lower);
    }
    if (_upperLatch.update(temp, ts.upper_threshold, cmp, hyst) && ts.program_id_upper != 0) {
        logger.log(LogModule::ThermostatManager, "Upper threshold reached, starting program %u\n", ts.program_id_upper);
        _executor.start(ts.program_id_upper);
    }
}
