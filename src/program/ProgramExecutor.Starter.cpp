// src/program/ProgramExecutor.Starter.cpp
#include "program/ProgramExecutor.h"

#include "config/Config.h"
#include "config/VehicleRules.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "io/DigitalInputs.h"
#include "io/HwMap.h"
#include "io/RelayController.h"
#include "io/SensorsController.h"
#include "common/Logger.h"
#include "common/Utils.h"

bool ProgramExecutor::isInStarterStep() const {
    if (!_running) return false;
    if (!_localActions) return false;
    if (_currentStep >= _localStepCount) return false;
    return isStarterAction(_localActions[_currentStep]);
}

bool ProgramExecutor::isStartBlocked() const {
    const uint16_t blockId = config.getBase().vehicle.start_block_input_id;
    if (blockId == 0) return false;
    const int8_t idx = HwMap::inputIndexById(blockId);
    // Missing, non-digital or disabled interlock input fails safe: start is blocked.
    if (idx < 0) return true;
    if (config.getBase().inputs[idx].type != InputType::DIGITAL) return true;
    DigitalInputs& inputs = core.getInputs();
    if (!inputs.isRuntimeEnabled((uint8_t)idx)) return true;
    return inputs.getState((uint8_t)idx);
}

void ProgramExecutor::failStart(const char* reason, ErrorCode code) {
    logger.log("[Starter] start failed: %s, all relays OFF\n", reason);
    core.getRelay().allOff();
    finish(false);
    if (code != ErrorCode::NONE) core.getErrorManager().set(code);
}

void ProgramExecutor::beginStarter() {
    const BaseConfig& base = config.getBase();
    const VehicleConfig& v = base.vehicle;

    _starterRelayIdx = HwMap::relayIndexById(v.starter_relay_id);
    if (_starterRelayIdx < 0) {
        abortInvalidRef("starter_relay_id", v.starter_relay_id);
        return;
    }

    _startInputIdx = -1;
    if (v.start_method != StartMethod::Timed) {
        if (const char* r = vehicle_rules::checkStartInput(base, v.start_method, v.start_input_id)) {
            logger.log("[Starter] %s (start_input_id=%u)\n", r, (unsigned)v.start_input_id);
            abortWithError("start input unusable", ErrorCode::PROGRAM_INVALID_REF);
            return;
        }
        _startInputIdx = HwMap::inputIndexById(v.start_input_id);
        if (_startInputIdx < 0 || !core.getInputs().isRuntimeEnabled((uint8_t)_startInputIdx)) {
            logger.log("[Starter] start input %u disabled at runtime\n", (unsigned)v.start_input_id);
            abortWithError("start input disabled", ErrorCode::PROGRAM_INVALID_REF);
            return;
        }
    }

    starter::Params p;
    p.method = v.start_method;
    p.crankMs = (uint32_t)(v.starter_crank_time_sec * 1000.0f + 0.5f);
    p.maxMs = secToMs((uint32_t)v.starter_max_time_sec);
    p.settleMs = secToMs((uint32_t)v.wait_after_start_sec);
    p.windowExtraMs = StarterLimits::VOLTAGE_WINDOW_EXTRA_MS;
    p.restMs = secToMs((uint32_t)v.starter_retry_pause_sec);
    p.attempts = v.starter_attempts;
    p.runningVoltage = v.engine_running_voltage_threshold;

    const uint32_t now = millis();
    _starter.begin(p, now);
    logger.log("[Starter] begin method=%s attempts=%u crank=%ums max=%ums settle=%ums rest=%ums vrun=%.2f\n",
               vehicle_rules::startMethodToString(p.method), (unsigned)_starter.params().attempts,
               (unsigned)_starter.params().crankMs, (unsigned)p.maxMs, (unsigned)p.settleMs,
               (unsigned)p.restMs, (double)p.runningVoltage);
    updateStarter(now);
}

void ProgramExecutor::logStarterAttempt() const {
    const starter::Attempt& a = _starter.last();
    char minV[12];
    if (a.hasMinVoltage) snprintf(minV, sizeof(minV), "%.2f", (double)a.minVoltage);
    else strlcpy(minV, "-", sizeof(minV));
    logger.log("[Starter] attempt %u/%u method=%s stop=%s crank=%ums vmin=%s vend=%.2f maxgap=%ums\n",
               (unsigned)a.number, (unsigned)_starter.params().attempts,
               vehicle_rules::startMethodToString(_starter.params().method),
               starter::stopReasonToString(a.reason), (unsigned)a.crankMs, minV, (double)a.finalVoltage,
               (unsigned)a.maxGapMs);
}

void ProgramExecutor::updateStarter(uint32_t now) {
    RelayController& relays = core.getRelay();
    DigitalInputs& inputs = core.getInputs();
    SensorsController& sensors = core.getSensors();
    const StartMethod method = _starter.params().method;

    starter::Sample s;
    s.nowMs = now;
    s.blocked = isStartBlocked();

    if (method != StartMethod::Timed) {
        if (_startInputIdx < 0 || !inputs.isRuntimeEnabled((uint8_t)_startInputIdx)) {
            abortWithError("start input disabled during engine start", ErrorCode::PROGRAM_INVALID_REF);
            return;
        }
        s.condition = (method == StartMethod::Rpm) ? inputs.isRpmConfirmed((uint8_t)_startInputIdx)
                                                   : inputs.getState((uint8_t)_startInputIdx);
    }
    s.voltageValid = sensors.isVoltageValid();
    s.voltageWindowFull = sensors.isVoltageWindowFull();
    s.voltage = sensors.getVoltage();

    if (_starter.phase() == starter::Phase::Cranking && !relays.getState((uint8_t)_starterRelayIdx)) {
        failStart("starter relay cut by safety limit");
        return;
    }

    const starter::Tick t = _starter.tick(s);

    if (t.relay == starter::RelayCmd::On) {
        if (!relays.on((uint8_t)_starterRelayIdx)) {
            failStart("starter relay locked out");
            return;
        }
    } else if (t.relay == starter::RelayCmd::Off) {
        relays.off((uint8_t)_starterRelayIdx);
    }
    if (t.resetVoltageWindow) sensors.resetVoltageWindow();
    if (t.attemptEnded) logStarterAttempt();

    switch (t.outcome) {
        case starter::Outcome::Success:
            logger.log("[Starter] engine running\n");
            nextStep();
            return;
        case starter::Outcome::Failure:
            failStart("no attempts left");
            return;
        case starter::Outcome::Blocked:
            failStart("blocked by start_block_input", ErrorCode::START_BLOCKED);
            return;
        case starter::Outcome::Running:
        default:
            return;
    }
}
