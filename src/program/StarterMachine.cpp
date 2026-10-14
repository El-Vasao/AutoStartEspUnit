// src/program/StarterMachine.cpp
#include "program/StarterMachine.h"

namespace starter {

void Machine::begin(const Params& p, uint32_t nowMs) {
    p_ = p;
    if (p_.attempts == 0) p_.attempts = 1;
    if (p_.crankMs > p_.maxMs) p_.crankMs = p_.maxMs;
    phase_ = Phase::Idle;
    attempt_ = 0;
    phaseStartMs_ = nowMs;
    crankMs_ = 0;
    lastTickMs_ = nowMs;
    haveTick_ = false;
    haveMin_ = false;
    minVoltage_ = 0.0f;
    maxGapMs_ = 0;
    last_ = Attempt{};
}

bool Machine::alreadyRunning(const Sample& s) const {
    if (p_.method == StartMethod::Timed) {
        return s.voltageValid && s.voltage >= p_.runningVoltage;
    }
    return s.condition;
}

void Machine::endAttempt(StopReason reason, const Sample& s, Tick& t) {
    last_.number = attempt_;
    last_.reason = reason;
    last_.crankMs = crankMs_;
    last_.hasMinVoltage = haveMin_;
    last_.minVoltage = minVoltage_;
    last_.finalVoltage = s.voltage;
    last_.maxGapMs = maxGapMs_;
    t.attemptEnded = true;
}

void Machine::failOrRest(const Sample& s, Tick& t) {
    if (attempt_ < p_.attempts) {
        phase_ = Phase::Resting;
        phaseStartMs_ = s.nowMs;
    } else {
        phase_ = Phase::Idle;
        t.outcome = Outcome::Failure;
    }
}

Tick Machine::tick(const Sample& s) {
    Tick t;
    const uint32_t now = s.nowMs;
    if (haveTick_ && phase_ == Phase::Cranking) {
        const uint32_t gap = now - lastTickMs_;
        if (gap > maxGapMs_) maxGapMs_ = gap;
    }
    haveTick_ = true;
    lastTickMs_ = now;

    if (s.blocked) {
        t.relay = RelayCmd::Off;
        t.outcome = Outcome::Blocked;
        if (phase_ == Phase::Cranking) {
            crankMs_ = now - phaseStartMs_;
            endAttempt(StopReason::Blocked, s, t);
        }
        phase_ = Phase::Idle;
        return t;
    }

    switch (phase_) {
        case Phase::Resting:
            if (now - phaseStartMs_ < p_.restMs) return t;
            phase_ = Phase::Idle;
            [[fallthrough]];
        case Phase::Idle:
            if (alreadyRunning(s)) {
                t.relay = RelayCmd::Off;
                t.outcome = Outcome::Success;
                crankMs_ = 0;
                haveMin_ = false;
                maxGapMs_ = 0;
                endAttempt(StopReason::AlreadyRunning, s, t);
                return t;
            }
            attempt_++;
            phase_ = Phase::Cranking;
            phaseStartMs_ = now;
            crankMs_ = 0;
            maxGapMs_ = 0;
            haveMin_ = s.voltageValid;
            minVoltage_ = s.voltage;
            t.relay = RelayCmd::On;
            return t;

        case Phase::Cranking: {
            if (s.voltageValid && (!haveMin_ || s.voltage < minVoltage_)) {
                haveMin_ = true;
                minVoltage_ = s.voltage;
            }
            const uint32_t elapsed = now - phaseStartMs_;
            if (p_.method != StartMethod::Timed && s.condition) {
                t.relay = RelayCmd::Off;
                crankMs_ = elapsed;
                phase_ = Phase::Idle;
                t.outcome = Outcome::Success;
                endAttempt(StopReason::Condition, s, t);
                return t;
            }
            if (p_.method == StartMethod::Timed && elapsed >= p_.crankMs) {
                t.relay = RelayCmd::Off;
                crankMs_ = elapsed;
                phase_ = Phase::Settling;
                phaseStartMs_ = now;
                t.resetVoltageWindow = true;
                return t;
            }
            if (elapsed >= p_.maxMs) {
                t.relay = RelayCmd::Off;
                crankMs_ = elapsed;
                endAttempt(StopReason::MaxTime, s, t);
                failOrRest(s, t);
                return t;
            }
            return t;
        }

        case Phase::Settling: {
            const uint32_t elapsed = now - phaseStartMs_;
            if (elapsed < p_.settleMs) return t;
            const bool timedOut = elapsed >= p_.settleMs + p_.windowExtraMs;
            if (!(s.voltageValid && s.voltageWindowFull) && !timedOut) return t;
            if (s.voltageValid && s.voltage >= p_.runningVoltage) {
                phase_ = Phase::Idle;
                t.outcome = Outcome::Success;
                endAttempt(StopReason::VoltageOk, s, t);
                return t;
            }
            endAttempt(s.voltageValid && s.voltageWindowFull ? StopReason::VoltageLow : StopReason::NoVoltage, s, t);
            failOrRest(s, t);
            return t;
        }
    }
    return t;
}

const char* stopReasonToString(StopReason r) {
    switch (r) {
        case StopReason::AlreadyRunning: return "already_running";
        case StopReason::Condition:      return "condition";
        case StopReason::MaxTime:        return "max_time";
        case StopReason::VoltageOk:      return "voltage_ok";
        case StopReason::VoltageLow:     return "voltage_low";
        case StopReason::NoVoltage:      return "no_voltage";
        case StopReason::Blocked:        return "blocked";
        case StopReason::None:
        default:                         return "none";
    }
}

} // namespace starter
