// include/core/TriggerManager.h
#pragma once

#include <Arduino.h>
#include "core/TriggerRules.h"
#include "io/DigitalInputs.h"
#include "io/SensorsController.h"
#include "program/ProgramExecutor.h"

class Config;

/**
 * @brief Менеджер триггеров (входных и температурных).
 * Отслеживает состояния и запускает программы при срабатывании.
 *
 * Модель срабатывания:
 * - вход — фронт (`trigger_rules::EdgeLatch`);
 * - температура — фронт с гистерезисом отпускания (`ThresholdLatch`).
 *
 * Важно:
 * - если одновременно сработают несколько триггеров, они будут конкурировать за `ProgramExecutor`
 *   (новая программа сбивает предыдущую).
 */
class TriggerManager {
public:
    TriggerManager(Config& config, DigitalInputs& inputs, SensorsController& sensors, ProgramExecutor& executor);

    void begin();
    void update();

    /// Copy enabled flags from config without resetting latches (OTA restore).
    void syncEnabledFromConfig();

    /// When true, use Timing::TRIGGER_CHECK_INTERVAL_IDLE_MS.
    void setPollIdle(bool idle) { _pollIdle = idle; }

    void setInputTriggerEnabled(uint8_t index, bool en);
    bool isInputTriggerEnabled(uint8_t index) const;

    void setTempTriggerEnabled(uint8_t index, bool en);
    bool isTempTriggerEnabled(uint8_t index) const;

private:
    Config& _config;
    DigitalInputs& _inputs;
    SensorsController& _sensors;
    ProgramExecutor& _executor;

    bool _inputEnabled[Limits::MAX_TRIGGERS];
    trigger_rules::EdgeLatch _inputLatch[Limits::MAX_TRIGGERS];
    bool _tempEnabled[Limits::MAX_TRIGGERS];
    trigger_rules::ThresholdLatch _tempLatch[Limits::MAX_TRIGGERS];

    uint32_t _lastCheck;
    bool _pollIdle{false};
};
