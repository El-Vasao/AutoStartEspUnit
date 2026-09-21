// src/program/ProgramExecutor.Core.cpp
#include "program/ProgramExecutor.h"

#include "core/Core.h"
#include "io/RelayController.h"
#include "core/ErrorManager.h"
#include "io/HwMap.h"
#include "common/Logger.h"
#include "program/internal/ProgramExecutorInternal.h"

using namespace program_executor_internal;

ProgramExecutor::ProgramExecutor()
    : _programId(0),
      _running(false),
      _currentStep(0),
      _rt{0, 0},
      _starter{0, 0, 0},
      _lastProgramId(0),
      _localSteps(nullptr),
      _localActions(nullptr),
      _localStepCount(0) {
    _currentProgramName[0] = '\0';
    _lastProgramName[0] = '\0';
}

void ProgramExecutor::begin() {}

ActionId ProgramExecutor::currentActionId() const {
    if (!_running) return ActionId::UNKNOWN;
    if (_currentStep >= _localStepCount) return ActionId::UNKNOWN;
    return _localActions[_currentStep];
}

uint32_t ProgramExecutor::getTimerRemainingMs() const {
    if (!_running) return 0;
    if (!_localSteps || !_localActions) return 0;
    if (_currentStep >= _localStepCount) return 0;

    const CompiledStep& step = _localSteps[_currentStep];
    uint32_t now = millis();

    uint32_t planned = 0;
    switch (_localActions[_currentStep]) {
        case ActionId::WAIT:
            planned = step.ms;
            break;
        case ActionId::RELAY_PULSE_ON_OFF_ON:
        case ActionId::RELAY_PULSE_OFF_ON_OFF:
            if (_rt.phase != 1 && _rt.phase != 2) return 0;
            planned = step.ms;
            break;
        default:
            return 0;
    }

    uint32_t elapsed = now - _rt.startedAtMs;
    if (elapsed >= planned) return 0;
    return planned - elapsed;
}

bool ProgramExecutor::start(uint8_t programId) {
    stop();

    _localSteps = _localStepsArr;
    _localActions = _localActionsArr;
    _currentProgramName[0] = '\0';
    _localStepCount = 0;
    if (!config.loadProgramCompiled(programId, _localSteps, Limits::MAX_STEPS_PER_PROGRAM,
                                    &_localStepCount, _currentProgramName, sizeof(_currentProgramName))) {
        logger.log("[ProgramExecutor] Program %u not found or invalid\n", programId);
        _localActions = nullptr;
        _localSteps = nullptr;
        _localStepCount = 0;
        return false;
    }
    if (_localStepCount == 0) {
        logger.log("[ProgramExecutor] Program %u is empty\n", programId);
        _localActions = nullptr;
        _localSteps = nullptr;
        return false;
    }
    for (uint8_t i = 0; i < _localStepCount; i++) {
        _localActions[i] = _localSteps[i].action;
    }

    _programId = programId;
    _running = true;
    _currentStep = 0;
    _rt.phase = 0;
    _rt.startedAtMs = 0;
    _starter.phase = 0;
    _starter.startedAtMs = 0;
    _starter.attemptsLeft = 0;

    logger.log("[ProgramExecutor] Starting program %u: %s (steps=%u)\n",
               programId, _currentProgramName, (unsigned)_localStepCount);

    const CompiledStep& firstStep = _localSteps[0];
    executeStep(firstStep, _localActions[0]);
    return true;
}

void ProgramExecutor::stop() {
    if (_running) {
        logger.log("[ProgramExecutor] Stopping program id=%u name=%s at step=%u/%u\n",
                   (unsigned)_programId, _currentProgramName, (unsigned)_currentStep, (unsigned)_localStepCount);
        if (_currentStep < _localStepCount) {
            const ActionId id = _localActions[_currentStep];
            if (id == ActionId::STARTER_TIMED || id == ActionId::STARTER_WAIT_INPUT) {
                const uint16_t starterRelayId = config.getBase().vehicle.starter_relay_id;
                const int8_t starterRelay = HwMap::relayIndexById(starterRelayId);
                if (starterRelay >= 0) core.getRelay().off((uint8_t)starterRelay);
            }
        }
        finish();
    }
}

void ProgramExecutor::nextStep() {
    if (!_running) return;

    _currentStep++;
    if (_currentStep >= _localStepCount) {
        finish();
    } else {
        const CompiledStep& next = _localSteps[_currentStep];
        executeStep(next, _localActions[_currentStep]);
    }
}

void ProgramExecutor::finish() {
    logger.log("[ProgramExecutor] Program finished id=%u name=%s\n",
               (unsigned)_programId, _currentProgramName);

    if (_programId != 0 && _currentProgramName[0] != '\0') {
        _lastProgramId = _programId;
        strlcpy(_lastProgramName, _currentProgramName, sizeof(_lastProgramName));
    }
    _running = false;
    _programId = 0;
    _currentProgramName[0] = '\0';
    _localActions = nullptr;
    _localSteps = nullptr;
    _localStepCount = 0;
}

void ProgramExecutor::abortWithError() {
    logger.log("[ProgramExecutor] ⚠️ Program aborted due to configuration change\n");
    core.getRelay().allOff();
    finish();
    core.getErrorManager().set(ErrorCode::PROGRAM_ABORTED);
}

