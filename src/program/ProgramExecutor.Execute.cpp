// src/program/ProgramExecutor.Execute.cpp
#include "program/ProgramExecutor.h"

#include "core/Core.h"
#include "io/DigitalInputs.h"
#include "core/ErrorManager.h"
#include "io/HwMap.h"
#include "common/Logger.h"
#include "io/RelayController.h"
#include "program/internal/ProgramExecutorInternal.h"

using namespace program_executor_internal;

void ProgramExecutor::update() {
    if (!_running) return;

    if (!_localSteps || !_localActions || _currentStep >= _localStepCount) {
        abortWithError();
        return;
    }

    const CompiledStep& step = _localSteps[_currentStep];
    const ActionId actionId = _localActions[_currentStep];
    uint32_t now = millis();
    const int8_t relayIdx = HwMap::relayIndexById(step.relay_id);

    switch (actionId) {
        case ActionId::WAIT:
            if (now - _rt.startedAtMs >= step.ms) nextStep();
            return;

        case ActionId::RELAY_PULSE_ON_OFF_ON:
            if (_rt.phase == 1) {
                if (now - _rt.startedAtMs < step.ms) return;
                if (relayIdx < 0) {
                    logger.log("[ProgramExecutor] Invalid relay_id %u, aborting program\n", (unsigned)step.relay_id);
                    abortWithError();
                    core.getErrorManager().set(ErrorCode::PROGRAM_INVALID_REF);
                    return;
                }
                core.getRelay().off((uint8_t)relayIdx);
                _rt.phase = 2;
                _rt.startedAtMs = now;
                return;
            }
            if (_rt.phase == 2) {
                if (now - _rt.startedAtMs < step.ms) return;
                if (relayIdx < 0) {
                    logger.log("[ProgramExecutor] Invalid relay_id %u, aborting program\n", (unsigned)step.relay_id);
                    abortWithError();
                    core.getErrorManager().set(ErrorCode::PROGRAM_INVALID_REF);
                    return;
                }
                core.getRelay().on((uint8_t)relayIdx);
                _rt.phase = 0;
                nextStep();
                return;
            }
            return;

        case ActionId::RELAY_PULSE_OFF_ON_OFF:
            if (_rt.phase == 1) {
                if (now - _rt.startedAtMs < step.ms) return;
                if (relayIdx < 0) {
                    logger.log("[ProgramExecutor] Invalid relay_id %u, aborting program\n", (unsigned)step.relay_id);
                    abortWithError();
                    core.getErrorManager().set(ErrorCode::PROGRAM_INVALID_REF);
                    return;
                }
                core.getRelay().on((uint8_t)relayIdx);
                _rt.phase = 2;
                _rt.startedAtMs = now;
                return;
            }
            if (_rt.phase == 2) {
                if (now - _rt.startedAtMs < step.ms) return;
                if (relayIdx < 0) {
                    logger.log("[ProgramExecutor] Invalid relay_id %u, aborting program\n", (unsigned)step.relay_id);
                    abortWithError();
                    core.getErrorManager().set(ErrorCode::PROGRAM_INVALID_REF);
                    return;
                }
                core.getRelay().off((uint8_t)relayIdx);
                _rt.phase = 0;
                nextStep();
                return;
            }
            return;

        case ActionId::STARTER_TIMED:
        case ActionId::STARTER_WAIT_INPUT: {
            const auto& vcfg = config.getBase().vehicle;
            const uint16_t starterRelayId = vcfg.starter_relay_id;
            const int8_t starterRelay = HwMap::relayIndexById(starterRelayId);
            if (starterRelay < 0) {
                logger.log("[ProgramExecutor] Invalid starter_relay_id %u, aborting program\n",
                           (unsigned)starterRelayId);
                abortWithError();
                core.getErrorManager().set(ErrorCode::PROGRAM_INVALID_REF);
                return;
            }

            if (_starter.attemptsLeft == 0) _starter.attemptsLeft = (step.retries ? step.retries : 1);

            const uint32_t maxStarterMs = (uint32_t)vcfg.starter_max_time_sec * 1000UL;
            const uint32_t attemptMs =
                (actionId == ActionId::STARTER_TIMED) ? ((step.ms > 0 && step.ms < maxStarterMs) ? step.ms : maxStarterMs)
                                                     : maxStarterMs;

            if (_starter.phase == 1) {
                bool stop = false;
                if (actionId == ActionId::STARTER_WAIT_INPUT) {
                    const int8_t inIdx = HwMap::inputIndexById(step.input_id);
                    if (inIdx < 0) {
                        logger.log("[ProgramExecutor] Invalid input_id %u, aborting program\n", (unsigned)step.input_id);
                        core.getRelay().off((uint8_t)starterRelay);
                        abortWithError();
                        core.getErrorManager().set(ErrorCode::PROGRAM_INVALID_REF);
                        return;
                    }
                    if (core.getInputs().getState((uint8_t)inIdx)) stop = true;
                }
                if (!stop && (now - _starter.startedAtMs >= attemptMs)) stop = true;

                if (stop) {
                    core.getRelay().off((uint8_t)starterRelay);
                    _starter.phase = 2;
                    _starter.startedAtMs = now;
                }
                return;
            }

            if (_starter.phase == 2) {
                const uint32_t waitAfterMs = (uint32_t)vcfg.wait_after_start_sec * 1000UL;
                if (now - _starter.startedAtMs < waitAfterMs) return;

                if (core.isEngineRunning()) {
                    _starter.phase = 0;
                    _starter.attemptsLeft = 0;
                    nextStep();
                    return;
                }

                if (_starter.attemptsLeft > 1) {
                    _starter.attemptsLeft--;
                    _starter.phase = 3;
                    _starter.startedAtMs = now;
                    return;
                }

                logger.log("[ProgramExecutor] Starter failed after retries, finishing program\n");
                _starter.phase = 0;
                _starter.attemptsLeft = 0;
                finish(false);
                return;
            }

            if (_starter.phase == 3) {
                if (now - _starter.startedAtMs < 1000UL) return;
                core.getRelay().on((uint8_t)starterRelay);
                _starter.phase = 1;
                _starter.startedAtMs = now;
                return;
            }

            core.getRelay().on((uint8_t)starterRelay);
            _starter.phase = 1;
            _starter.startedAtMs = now;
            return;
        }

        default:
            return;
    }
}

void ProgramExecutor::executeStep(const CompiledStep& step, ActionId actionId) {
    logger.log("[ProgramExecutor] step %u/%u actionId=%u relay_id=%u input_id=%u ms=%u timeout_ms=%u\n",
               (unsigned)(_currentStep + 1),
               (unsigned)_localStepCount,
               (unsigned)actionId,
               (unsigned)step.relay_id,
               (unsigned)step.input_id,
               (unsigned)step.ms,
               (unsigned)step.timeout_ms);

    _rt.phase = 0;
    _starter.phase = 0;
    _starter.attemptsLeft = 0;

    switch (actionId) {
        case ActionId::RELAY_ON: {
            const int8_t idx = HwMap::relayIndexById(step.relay_id);
            if (idx < 0) {
                logger.log("[ProgramExecutor] Invalid relay_id %u, aborting program\n", (unsigned)step.relay_id);
                abortWithError();
                core.getErrorManager().set(ErrorCode::PROGRAM_INVALID_REF);
                return;
            }
            core.getRelay().on((uint8_t)idx);
            nextStep();
            return;
        }

        case ActionId::RELAY_OFF: {
            const int8_t idx = HwMap::relayIndexById(step.relay_id);
            if (idx < 0) {
                logger.log("[ProgramExecutor] Invalid relay_id %u, aborting program\n", (unsigned)step.relay_id);
                abortWithError();
                core.getErrorManager().set(ErrorCode::PROGRAM_INVALID_REF);
                return;
            }
            core.getRelay().off((uint8_t)idx);
            nextStep();
            return;
        }

        case ActionId::RELAY_TOGGLE: {
            const int8_t idx = HwMap::relayIndexById(step.relay_id);
            if (idx < 0) {
                logger.log("[ProgramExecutor] Invalid relay_id %u, aborting program\n", (unsigned)step.relay_id);
                abortWithError();
                core.getErrorManager().set(ErrorCode::PROGRAM_INVALID_REF);
                return;
            }
            core.getRelay().toggle((uint8_t)idx);
            nextStep();
            return;
        }

        case ActionId::RELAY_PULSE_ON_OFF_ON: {
            const int8_t idx = HwMap::relayIndexById(step.relay_id);
            if (idx < 0) {
                logger.log("[ProgramExecutor] Invalid relay_id %u, aborting program\n", (unsigned)step.relay_id);
                abortWithError();
                core.getErrorManager().set(ErrorCode::PROGRAM_INVALID_REF);
                return;
            }
            core.getRelay().on((uint8_t)idx);
            _rt.phase = 1;
            _rt.startedAtMs = millis();
            return;
        }

        case ActionId::RELAY_PULSE_OFF_ON_OFF: {
            const int8_t idx = HwMap::relayIndexById(step.relay_id);
            if (idx < 0) {
                logger.log("[ProgramExecutor] Invalid relay_id %u, aborting program\n", (unsigned)step.relay_id);
                abortWithError();
                core.getErrorManager().set(ErrorCode::PROGRAM_INVALID_REF);
                return;
            }
            core.getRelay().off((uint8_t)idx);
            _rt.phase = 1;
            _rt.startedAtMs = millis();
            return;
        }

        case ActionId::WAIT:
            _rt.startedAtMs = millis();
            return;

        case ActionId::INPUT_ENABLE: {
            const int8_t idx = HwMap::inputIndexById(step.input_id);
            if (idx < 0) {
                logger.log("[ProgramExecutor] Invalid input_id %u, aborting program\n", (unsigned)step.input_id);
                abortWithError();
                core.getErrorManager().set(ErrorCode::PROGRAM_INVALID_REF);
                return;
            }
            core.getInputs().setRuntimeEnabled((uint8_t)idx, true);
            nextStep();
            return;
        }

        case ActionId::INPUT_DISABLE: {
            const int8_t idx = HwMap::inputIndexById(step.input_id);
            if (idx < 0) {
                logger.log("[ProgramExecutor] Invalid input_id %u, aborting program\n", (unsigned)step.input_id);
                abortWithError();
                core.getErrorManager().set(ErrorCode::PROGRAM_INVALID_REF);
                return;
            }
            core.getInputs().setRuntimeEnabled((uint8_t)idx, false);
            nextStep();
            return;
        }

        case ActionId::INPUT_TRIGGER_ENABLE:
        case ActionId::INPUT_TRIGGER_DISABLE: {
            const int8_t idx = findInputTriggerIndexById(step.input_trigger_id);
            if (idx < 0 || !validTriggerIndex((uint8_t)idx)) {
                logInvalidTriggerAndSkip((uint8_t)idx, "input_trigger");
                nextStep();
                return;
            }
            core.setTriggerRuntime((uint8_t)idx, actionId == ActionId::INPUT_TRIGGER_ENABLE);
            nextStep();
            return;
        }

        case ActionId::TEMP_TRIGGER_ENABLE:
        case ActionId::TEMP_TRIGGER_DISABLE: {
            const int8_t idx = findTempTriggerIndexById(step.temp_trigger_id);
            if (idx < 0 || !validTriggerIndex((uint8_t)idx)) {
                logInvalidTriggerAndSkip((uint8_t)idx, "temp_trigger");
                nextStep();
                return;
            }
            core.setTempTriggerRuntime((uint8_t)idx, actionId == ActionId::TEMP_TRIGGER_ENABLE);
            nextStep();
            return;
        }

        case ActionId::BATTERY_SAVER_ON:
            core.setBatterySaverRuntime(true);
            nextStep();
            return;

        case ActionId::BATTERY_SAVER_OFF:
            core.setBatterySaverRuntime(false);
            nextStep();
            return;

        case ActionId::THERMOSTAT_ON:
            core.setThermostatRuntime(true);
            nextStep();
            return;

        case ActionId::THERMOSTAT_OFF:
            core.setThermostatRuntime(false);
            nextStep();
            return;

        case ActionId::STARTER_TIMED:
        case ActionId::STARTER_WAIT_INPUT:
            _starter.phase = 0;
            _starter.startedAtMs = 0;
            _starter.attemptsLeft = 0;
            // Actual starter work happens in update().
            return;

        case ActionId::UNKNOWN:
        default:
            logger.log("[ProgramExecutor] Unknown actionId %u, skipping step\n", (unsigned)actionId);
            nextStep();
            return;
    }
}

