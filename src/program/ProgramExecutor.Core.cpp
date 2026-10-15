// src/program/ProgramExecutor.Core.cpp
#include "program/ProgramExecutor.h"

#include "core/Core.h"
#include "gsm/GsmController.h"
#include "io/RelayController.h"
#include "core/ErrorManager.h"
#include "io/HwMap.h"
#include "common/Logger.h"
#include "config/Config.h"
#include "program/ProgramValidate.h"
#include "program/internal/ProgramExecutorInternal.h"

using namespace program_executor_internal;

ProgramExecutor::ProgramExecutor()
    : _programId(0),
      _running(false),
      _currentStep(0),
      _rt{0, 0},
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
    if (isInStarterStep()) {
        logger.log(LogModule::ProgramExecutor, "Start of program %u rejected: engine start in progress (program %u)\n",
                   (unsigned)programId, (unsigned)_programId);
        return false;
    }
    stop();

    _localSteps = _localStepsArr;
    _localActions = _localActionsArr;
    _currentProgramName[0] = '\0';
    _localStepCount = 0;
    if (!config.loadProgramCompiled(programId, _localSteps, Limits::MAX_STEPS_PER_PROGRAM,
                                    &_localStepCount, _currentProgramName, sizeof(_currentProgramName))) {
        logger.log(LogModule::ProgramExecutor, "Program %u not found or invalid\n", programId);
        _localActions = nullptr;
        _localSteps = nullptr;
        _localStepCount = 0;
        return false;
    }
    if (_localStepCount == 0) {
        logger.log(LogModule::ProgramExecutor, "Program %u is empty\n", programId);
        _localActions = nullptr;
        _localSteps = nullptr;
        return false;
    }
    const uint16_t starterRelayId = config.getBase().vehicle.starter_relay_id;
    for (uint8_t i = 0; i < _localStepCount; i++) {
        _localActions[i] = _localSteps[i].action;
        const program_validate::Issue issue =
            program_validate::checkStep(_localActions[i], _localSteps[i].relay_id, starterRelayId);
        if (issue != program_validate::Issue::None) {
            logger.log(LogModule::ProgramExecutor, "Program %u rejected: step %u %s\n", (unsigned)programId,
                       (unsigned)(i + 1), program_validate::issueToString(issue));
            _localActions = nullptr;
            _localSteps = nullptr;
            _localStepCount = 0;
            core.getErrorManager().set(ErrorCode::PROGRAM_INVALID_REF);
            return false;
        }
    }

    _programId = programId;
    _running = true;
    _currentStep = 0;
    _rt.phase = 0;
    _rt.startedAtMs = 0;

    logger.log(LogModule::ProgramExecutor, "Starting program %u: %s (steps=%u)\n",
               programId, _currentProgramName, (unsigned)_localStepCount);

    const CompiledStep& firstStep = _localSteps[0];
    executeStep(firstStep, _localActions[0]);
    return true;
}

void ProgramExecutor::stop() {
    // Preempt may interrupt CALL_OWNER/SMS_OWNER — abort modem epoch so ATD/CMGS is not orphaned.
    if (core.getGSM().modemServiceEpochBusy()) {
        core.getGSM().abortModemServiceEpoch();
    }
    if (_running) {
        logger.log(LogModule::ProgramExecutor, "Stopping program id=%u name=%s at step=%u/%u\n",
                   (unsigned)_programId, _currentProgramName, (unsigned)_currentStep, (unsigned)_localStepCount);
        if (isInStarterStep()) {
            logger.log(LogModule::ProgramExecutor, "Engine start interrupted, all relays OFF\n");
            core.getRelay().allOff();
        }
        finish(false);
    }
}

void ProgramExecutor::nextStep() {
    if (!_running) return;

    _currentStep++;
    if (_currentStep >= _localStepCount) {
        finish(true);
    } else {
        const CompiledStep& next = _localSteps[_currentStep];
        executeStep(next, _localActions[_currentStep]);
    }
}

void ProgramExecutor::finish(bool ok) {
    logger.log(LogModule::ProgramExecutor, "Program %s id=%u name=%s\n", ok ? "finished" : "stopped/failed",
               (unsigned)_programId, _currentProgramName);

    const uint8_t pid = _programId;
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

    if (_lifecycleCb && pid != 0) {
        _pendingLifecycle = true;
        _pendingLifecycleId = pid;
        _pendingLifecycleOk = ok;
    }
}

void ProgramExecutor::firePendingLifecycle() {
    if (!_pendingLifecycle) return;
    _pendingLifecycle = false;
    if (_lifecycleCb) _lifecycleCb(_lifecycleCtx, _pendingLifecycleId, _pendingLifecycleOk);
}

void ProgramExecutor::abortWithError(const char* reason, ErrorCode code) {
    logger.log(LogModule::ProgramExecutor, "⚠️ Program aborted: %s\n", reason ? reason : "-");
    core.getRelay().allOff();
    finish(false);
    core.getErrorManager().set(code);
}

void ProgramExecutor::abortInvalidRef(const char* what, unsigned id) {
    logger.log(LogModule::ProgramExecutor, "Invalid %s %u\n", what, id);
    abortWithError("invalid channel reference", ErrorCode::PROGRAM_INVALID_REF);
}

