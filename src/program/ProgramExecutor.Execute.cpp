// src/program/ProgramExecutor.Execute.cpp
#include "program/ProgramExecutor.h"

#include "core/Core.h"
#include "gsm/GSMController.h"
#include "io/DigitalInputs.h"
#include "core/ErrorManager.h"
#include "io/HwMap.h"
#include "common/Logger.h"
#include "common/Constants.h"
#include "common/Utils.h"
#include "io/RelayController.h"
#include "program/internal/ProgramExecutorInternal.h"

using namespace program_executor_internal;

void ProgramExecutor::update() {
    if (!_running) return;

    if (!_localSteps || !_localActions || _currentStep >= _localStepCount) {
        abortWithError("step table lost");
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
                    abortInvalidRef("relay_id", step.relay_id);
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
                    abortInvalidRef("relay_id", step.relay_id);
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
                    abortInvalidRef("relay_id", step.relay_id);
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
                    abortInvalidRef("relay_id", step.relay_id);
                    return;
                }
                core.getRelay().off((uint8_t)relayIdx);
                _rt.phase = 0;
                nextStep();
                return;
            }
            return;

        case ActionId::ENGINE_START:
            updateStarter(now);
            return;

        case ActionId::CALL_OWNER:
        case ActionId::SMS_OWNER: {
            bool ok = false;
            if (!core.getGSM().takeVoiceOpResult(ok)) return;
            if (ok) {
                nextStep();
            } else {
                logger.log("[ProgramExecutor] %s failed\n",
                           actionId == ActionId::CALL_OWNER ? "CALL_OWNER" : "SMS_OWNER");
                finish(false);
            }
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

    switch (actionId) {
        case ActionId::RELAY_ON: {
            const int8_t idx = HwMap::relayIndexById(step.relay_id);
            if (idx < 0) {
                abortInvalidRef("relay_id", step.relay_id);
                return;
            }
            core.getRelay().on((uint8_t)idx);
            nextStep();
            return;
        }

        case ActionId::RELAY_OFF: {
            const int8_t idx = HwMap::relayIndexById(step.relay_id);
            if (idx < 0) {
                abortInvalidRef("relay_id", step.relay_id);
                return;
            }
            core.getRelay().off((uint8_t)idx);
            nextStep();
            return;
        }

        case ActionId::RELAY_TOGGLE: {
            const int8_t idx = HwMap::relayIndexById(step.relay_id);
            if (idx < 0) {
                abortInvalidRef("relay_id", step.relay_id);
                return;
            }
            core.getRelay().toggle((uint8_t)idx);
            nextStep();
            return;
        }

        case ActionId::RELAY_PULSE_ON_OFF_ON: {
            const int8_t idx = HwMap::relayIndexById(step.relay_id);
            if (idx < 0) {
                abortInvalidRef("relay_id", step.relay_id);
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
                abortInvalidRef("relay_id", step.relay_id);
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
                abortInvalidRef("input_id", step.input_id);
                return;
            }
            core.getInputs().setRuntimeEnabled((uint8_t)idx, true);
            nextStep();
            return;
        }

        case ActionId::INPUT_DISABLE: {
            const int8_t idx = HwMap::inputIndexById(step.input_id);
            if (idx < 0) {
                abortInvalidRef("input_id", step.input_id);
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

        case ActionId::ENGINE_START:
            beginStarter();
            return;

        case ActionId::CALL_OWNER: {
            // UI default ms=1s is too short for a ring; treat <3s as "use modem default".
            uint32_t ringMs = step.ms;
            if (ringMs < ProgramTiming::CALL_OWNER_MIN_RING_MS) ringMs = GSM::VOICE_DEFAULT_RING_MS;
            if (!core.getGSM().requestCallOwner(ringMs)) {
                logger.log("[ProgramExecutor] CALL_OWNER rejected (no phone / GSM busy)\n");
                finish(false);
                return;
            }
            return;
        }

        case ActionId::SMS_OWNER: {
            if (!step.message[0]) {
                logger.log("[ProgramExecutor] SMS_OWNER empty message\n");
                finish(false);
                return;
            }
            if (!core.getGSM().requestSmsOwner(step.message)) {
                logger.log("[ProgramExecutor] SMS_OWNER rejected (no phone / GSM busy)\n");
                finish(false);
                return;
            }
            return;
        }

        case ActionId::UNKNOWN:
        default:
            logger.log("[ProgramExecutor] Unknown actionId %u, skipping step\n", (unsigned)actionId);
            nextStep();
            return;
    }
}
