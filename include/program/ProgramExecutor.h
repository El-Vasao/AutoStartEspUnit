// include/program/ProgramExecutor.h
#pragma once

#include <Arduino.h>
#include "common/Constants.h"
#include "program/CompiledStep.h"
#include "program/ProgramAction.h"

/**
 * @brief Исполнитель программ.
 * Отвечает за выполнение последовательности шагов программы,
 * управление реле и отслеживание состояния.
 *
 * Контракт:
 * - `start(id)` загружает программу из `Config` и делает локальную копию шагов (устойчиво к изменениям в ФС/JSON).
 * - выполнение происходит “с таймингом” в `update()`, поэтому `update()` должен вызываться часто.
 *
 * Инварианты:
 * - аварийные завершения выключают все реле (`RelayController::allOff()`), чтобы не оставить активные цепи.
 */
class ProgramExecutor {
public:
    ProgramExecutor();

    void begin();
    void update();

    bool start(uint8_t programId);
    void stop();

    bool isRunning() const { return _running; }
    const char* getCurrentProgramName() const { return _currentProgramName; }
    const char* getLastProgramName() const { return _lastProgramName; }
    uint8_t getCurrentStep() const { return _currentStep; }
    uint8_t getCurrentProgramId() const { return _programId; }
    uint8_t getLastProgramId() const { return _lastProgramId; }

    // Оставшееся время для “таймерного” шага (RELAY_PULSE_*), иначе 0.
    uint32_t getTimerRemainingMs() const;

    // Возвращает true только во время выполнения шага STARTER_*.
    bool isInStarterStep() const;

private:
    struct StepRuntime {
        uint8_t phase;        ///< 0 = init/idle, >0 = определяется действием
        uint32_t startedAtMs; ///< отметка времени для текущей фазы
    };

    struct StarterRuntime {
        uint8_t phase;         ///< 0..3 (см. реализацию)
        uint32_t startedAtMs;  ///< отметка времени для текущей фазы
        uint8_t attemptsLeft;  ///< оставшиеся попытки, 0 = не используется
    };

    uint8_t _programId;                 ///< ID текущей программы (0, если не запущена)
    bool _running;                       ///< Флаг выполнения программы
    uint8_t _currentStep;                 ///< Номер текущего шага (0..step_count-1)
    StepRuntime _rt;                      ///< runtime-состояние для RELAY_PULSE_*
    StarterRuntime _starter;              ///< runtime-состояние для STARTER_*
    char _currentProgramName[TextBytes::Programs::NAME]; ///< Имя текущей программы (для отладки/вывода)
    uint8_t _lastProgramId;                ///< ID последней запущенной программы
    char _lastProgramName[TextBytes::Programs::NAME]; ///< Имя последней запущенной программы (для UI/статуса)

    // Локальная копия шагов текущей программы (фиксированные массивы).
    CompiledStep _localStepsArr[Limits::MAX_STEPS_PER_PROGRAM];
    ActionId _localActionsArr[Limits::MAX_STEPS_PER_PROGRAM];
    CompiledStep* _localSteps;                            ///< указатель на _localStepsArr пока программа бежит
    ActionId* _localActions;                             ///< указатель на _localActionsArr пока программа бежит
    uint8_t _localStepCount;                          ///< количество шагов в локальной копии

    static ActionId compileAction(const char* action);
    ActionId currentActionId() const;

    void executeStep(const CompiledStep& step, ActionId actionId);
    void nextStep();
    void finish();
    void abort();
    void abortWithError();
};

