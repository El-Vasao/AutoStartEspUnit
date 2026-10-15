// src/core/Core.ThermostatManager.cpp
#include "core/ThermostatManager.h"
#include "config/Config.h"
#include "common/Logger.h"

/**
 * @file Core.ThermostatManager.cpp
 * @brief Логика термостата (пороговые условия → запуск программ).
 *
 * Инварианты:
 * - Неблокирующий `update()`.
 * - Запуск программ только по фронту условия (false→true), чтобы избежать “перезапуска в каждом цикле”.
 *
 * Память:
 * - Без heap/`String`.
 */

ThermostatManager::ThermostatManager(Config& config, SensorsController& sensors, ProgramExecutor& executor) :
    _config(config),
    _sensors(sensors),
    _executor(executor),
    _runtimeEnabled(false),
    _lastStateLower(false),
    _lastStateUpper(false)
{}

void ThermostatManager::begin() {
    logger.log("[ThermostatManager] begin\n");
    _runtimeEnabled = _config.getBase().thermostat.enabled;
    logger.log("[ThermostatManager] Runtime enabled: %d\n", _runtimeEnabled);
}

void ThermostatManager::update() {
    if (!_runtimeEnabled) {
        _lastStateLower = false;
        _lastStateUpper = false;
        return;
    }

    const auto& ts = _config.getBase().thermostat;
    if (!ts.enabled) return;

    const char* rom = "";
    for (uint8_t j = 0; j < HardwareLimits::SENSORS; j++) {
        if (_config.getBase().sensors[j].id == ts.sensor_id) {
            rom = _config.getBase().sensors[j].rom;
            break;
        }
    }
    const int8_t idx = _sensors.findSensorIndexByRom(rom);
    if (idx < 0) return;
    float temp = _sensors.getTemperature((uint8_t)idx);
    if (!_sensors.isTemperatureValid((uint8_t)idx)) return;

    // Срабатываем только на фронте (false -> true), иначе при стабильном “выше/ниже порога”
    // программа перезапускалась бы в каждом цикле.
    bool lower = false, upper = false;
    if (strcmp(ts.comparison, "above") == 0) {
        lower = (temp > ts.lower_threshold);
        upper = (temp > ts.upper_threshold);
    } else if (strcmp(ts.comparison, "below") == 0) {
        lower = (temp < ts.lower_threshold);
        upper = (temp < ts.upper_threshold);
    }

    if (lower && ts.program_id_lower != 0) {
        if (!_lastStateLower) {
            logger.log("[ThermostatManager] Lower threshold reached, starting program %u\n", ts.program_id_lower);
            _executor.start(ts.program_id_lower);
            _lastStateLower = true;
        }
    } else {
        _lastStateLower = false;
    }

    if (upper && ts.program_id_upper != 0) {
        if (!_lastStateUpper) {
            logger.log("[ThermostatManager] Upper threshold reached, starting program %u\n", ts.program_id_upper);
            _executor.start(ts.program_id_upper);
            _lastStateUpper = true;
        }
    } else {
        _lastStateUpper = false;
    }
}

