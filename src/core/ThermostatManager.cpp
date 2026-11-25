// src/core/ThermostatManager.cpp
#include "core/ThermostatManager.h"
#include "config/Config.h"
#include "config/VehicleRules.h"
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
    logger.log(LogModule::ThermostatManager, "start\n");
    const auto& ts = _config.getBase().thermostat;
    const bool ready = vehicle_rules::thermostatReady(ts);
    _runtimeEnabled = ts.enabled && ready;
    if (ts.enabled && !ready) {
        logger.log(LogModule::ThermostatManager, "enabled but config incomplete — runtime off\n");
    }
    _lowerLatch.reset();
    _upperLatch.reset();
    if (_runtimeEnabled) {
        const char* rom = "";
        for (uint8_t j = 0; j < HardwareLimits::SENSORS; j++) {
            if (_config.getBase().sensors[j].id == ts.sensor_id) {
                rom = _config.getBase().sensors[j].rom;
                break;
            }
        }
        const int8_t idx = _sensors.findSensorIndexByRom(rom);
        if (idx >= 0 && _sensors.isTemperatureValid((uint8_t)idx)) {
            const float temp = _sensors.getTemperature((uint8_t)idx);
            _lowerLatch.seed(temp, ts.lower_threshold, trigger_rules::Compare::Below);
            _upperLatch.seed(temp, ts.upper_threshold, trigger_rules::Compare::Above);
        }
    }
    logger.log(LogModule::ThermostatManager, "runtime on: %d\n", _runtimeEnabled);
}

void ThermostatManager::update() {
    const auto& ts = _config.getBase().thermostat;
    if (!_runtimeEnabled || !vehicle_rules::thermostatReady(ts)) {
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
        // Битый замер не сбрасывает гистерезис — иначе следующий хороший снова фронт.
        return;
    }
    const float temp = _sensors.getTemperature((uint8_t)idx);
    const float hyst = DS18B20::RELEASE_HYSTERESIS_C;
    // comparison в JSON не задаёт направление: нижний порог всегда «ниже», верхний всегда «выше».

    // Неудачный start не сбрасывает защёлку: иначе битый id крутит allOff каждый проход.
    if (_lowerLatch.update(temp, ts.lower_threshold, trigger_rules::Compare::Below, hyst) && ts.program_id_lower != 0) {
        logger.log(LogModule::ThermostatManager, "lower threshold, program %u start\n", ts.program_id_lower);
        (void)_executor.start(ts.program_id_lower);
    }
    if (_upperLatch.update(temp, ts.upper_threshold, trigger_rules::Compare::Above, hyst) && ts.program_id_upper != 0) {
        logger.log(LogModule::ThermostatManager, "upper threshold, program %u start\n", ts.program_id_upper);
        (void)_executor.start(ts.program_id_upper);
    }
}
