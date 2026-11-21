// include/program/ProgramExecutor.h
#pragma once

#include <Arduino.h>
#include "common/Constants.h"
#include "common/ErrorCodes.h"
#include "program/CompiledStep.h"
#include "program/ProgramAction.h"
#include "program/StarterMachine.h"

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
 * - явный `stop()`, `abortWithError` и провал ENGINE_START выключают все реле (`allOff()`);
 * - удачная передача управления (`start` поверх идущей) реле не гасит; если крутил стартер — только его;
 * - неудачная передача завершает текущую и делает `allOff()` (выходы без хозяина).
 */
class ProgramExecutor {
    friend struct ProgramExecutorTestFlags;

public:
    ProgramExecutor();

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

    // Возвращает true только во время выполнения шага ENGINE_START.
    bool isInStarterStep() const;

    /// MQTT / UI: finished=true штатно; false = остановка/прерывание.
    using LifecycleCb = void (*)(void* ctx, uint8_t programId, bool finishedOk);
    void setLifecycleCallback(LifecycleCb cb, void* ctx) {
        _lifecycleCb = cb;
        _lifecycleCtx = ctx;
    }

private:
    struct StepRuntime {
        uint8_t phase;        ///< 0 = init/idle, >0 = определяется действием
        uint32_t startedAtMs; ///< отметка времени для текущей фазы
    };

    uint8_t _programId;                 ///< ID текущей программы (0, если не запущена)
    bool _running;                       ///< Флаг выполнения программы
    uint8_t _currentStep;                 ///< Номер текущего шага (0..step_count-1)
    StepRuntime _rt;                      ///< рабочее состояние для RELAY_PULSE_*
    starter::Machine _starter;            ///< автомат шага ENGINE_START
    int8_t _starterRelayIdx{-1};          ///< реле стартера на время шага
    int8_t _startInputIdx{-1};            ///< вход алгоритма rpm/input на время шага
    char _currentProgramName[TextBytes::Programs::NAME]; ///< Имя текущей программы (для отладки/вывода)
    uint8_t _lastProgramId;                ///< ID последней запущенной программы
    char _lastProgramName[TextBytes::Programs::NAME]; ///< Имя последней запущенной программы (для UI/статуса)

    // Локальная копия шагов текущей программы (фиксированные массивы).
    CompiledStep _localStepsArr[Limits::MAX_STEPS_PER_PROGRAM];
    ActionId _localActionsArr[Limits::MAX_STEPS_PER_PROGRAM];
    CompiledStep* _localSteps;                            ///< указатель на _localStepsArr пока программа бежит
    ActionId* _localActions;                             ///< указатель на _localActionsArr пока программа бежит
    uint8_t _localStepCount;                          ///< количество шагов в локальной копии

    LifecycleCb _lifecycleCb{nullptr};
    void* _lifecycleCtx{nullptr};
    bool _pendingLifecycle{false};
    uint8_t _pendingLifecycleId{0};
    bool _pendingLifecycleOk{false};

    void executeStep(const CompiledStep& step, ActionId actionId);
    void nextStep();
    void finish(bool ok);
    void firePendingLifecycle();
    /// Все реле выкл, программа завершается с ошибкой `code`; `reason` — в журнал.
    void abortWithError(const char* reason, ErrorCode code = ErrorCode::PROGRAM_ABORTED);
    void abortInvalidRef(const char* what, unsigned id);

    // ENGINE_START (ProgramExecutor.Starter.cpp)
    void beginStarter();
    void updateStarter(uint32_t now);
    bool isStartBlocked() const;
    /// Все реле выкл, программа завершается неудачей; `code` != NONE — фиксируется ошибка.
    void failStart(const char* reason, ErrorCode code = ErrorCode::NONE);
    void logStarterAttempt() const;
};

