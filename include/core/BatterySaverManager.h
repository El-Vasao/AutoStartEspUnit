// include/core/BatterySaverManager.h
#pragma once

#include <Arduino.h>
#include "io/SensorsController.h"
#include "program/ProgramExecutor.h"

class Config;

/**
 * @brief Менеджер функции Battery Saver.
 * Отслеживает напряжение и запускает программу при длительном низком напряжении.
 *
 * Защиты:
 * - `voltage_abort_threshold`: немедленный abort попыток и stop программы battery saver;
 * - минимальная длительность низкого напряжения до старта;
 * - минимальный интервал между попытками;
 * - лимит попыток в сутки (псевдо-“день” считается от первого запуска, без RTC/реального времени).
 */
class BatterySaverManager {
public:
    // Конструктор с зависимостями
    BatterySaverManager(Config& config, SensorsController& sensors, ProgramExecutor& executor);

    // Инициализация runtime-флага из конфига
    void begin();

    // Проверка условий (вызывается периодически)
    void update();

    // Включить/выключить Battery Saver в рантайме
    void setEnabled(bool en) { _runtimeEnabled = en; }

    // Проверить, включён ли Battery Saver в рантайме
    bool isEnabled() const { return _runtimeEnabled; }

private:
    Config& _config;                    ///< ссылка на конфигурацию
    SensorsController& _sensors;        ///< ссылка на контроллер сенсоров (для напряжения)
    ProgramExecutor& _executor;          ///< ссылка на исполнитель программ

    bool _runtimeEnabled;                ///< включён ли Battery Saver (может отключаться через веб)
    uint32_t _lastAttempt;                ///< время последней попытки запуска (мс)
    uint8_t _attemptsToday;               ///< количество попыток за текущий день
    uint32_t _dayStart;                   ///< время начала текущего дня (мс)
    uint32_t _lowStartTime;               ///< время начала низкого напряжения (мс)
};

