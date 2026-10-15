// include/core/ThermostatManager.h
#pragma once

#include <Arduino.h>
#include "core/TriggerRules.h"
#include "io/SensorsController.h"
#include "program/ProgramExecutor.h"

class Config;

/**
 * @brief Менеджер термостата.
 * Отслеживает температуру по заданному датчику и запускает программы при достижении порогов.
 *
 * Логика:
 * - два порога (lower/upper) и два действия (program_id_lower/program_id_upper);
 * - срабатывание на фронте с гистерезисом отпускания (`trigger_rules::ThresholdLatch`).
 */
class ThermostatManager {
public:
    ThermostatManager(Config& config, SensorsController& sensors, ProgramExecutor& executor);

    void begin();
    void update();

    void setEnabled(bool en) { _runtimeEnabled = en; }
    bool isEnabled() const { return _runtimeEnabled; }

private:
    Config& _config;
    SensorsController& _sensors;
    ProgramExecutor& _executor;

    bool _runtimeEnabled;
    trigger_rules::ThresholdLatch _lowerLatch;
    trigger_rules::ThresholdLatch _upperLatch;
};
