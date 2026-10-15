/**
 * @file GsmController.Ntp.cpp
 * @brief Non-blocking wall-clock cascade: CCLK/NITZ → CIPSHUT → CIPGSMLOC → CNTP → CCLK.
 */
#include "gsm/GsmController.h"

#include "common/Logger.h"
#include "common/Constants.h"
#include "gsm/GsmTimeParse.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

namespace {

/// Usable modem clock from a +CCLK snapshot. Fractional NITZ zones are logged and kept out of
/// tz_offset_hours (config/UI store whole hours only).
bool readCclkSnapshot(const char* snap, gsm_time::CclkTime& out) {
    if (!snap[0] || !gsm_time::parseCclk(snap, out)) return false;
    if (!out.tzWholeHours) {
        logger.log(LogModule::GsmController, "CCLK tzq=%d not whole hours; skip config TZ\n", (int)out.tzQuarters);
    }
    return gsm_time::epochSane(out.utc);
}

} // namespace

bool GsmController::requestTimeSync(const char* ntpServer, int8_t tzOffsetHours) {
    if (_timeStep != TimeStep::Idle) return false;
    if (_state != GsmState::READY) return false;
    if (modemServiceEpochBusy()) return false;
    if (tcpSocketActive() || _stack.tcp.isBusBusy()) return false;
    if (_await != AwaitKind::NONE) return false;

    if (ntpServer && ntpServer[0]) {
        strlcpy(_ntpServer, ntpServer, sizeof(_ntpServer));
    } else {
        _ntpServer[0] = '\0';
    }
    _ntpTzQuarters = gsm_time::tzHoursToQuarters(tzOffsetHours);
    _ntpUrcOk = false;
    _ntpUrcFail = false;
    _timeResultReady = false;
    _timeEpochUtc = 0;
    _timeSource = TimeSource::None;
    _timeTzHours = 0;
    _timeTzValid = false;
    _timeFailMask = 0;
    _timeFailReady = false;
    _timeCmdSent = false;
    _timeCclkOnly = false;
    _cclkSnap[0] = '\0';
    _cipgsmlocSnap[0] = '\0';
    _timeStep = TimeStep::CclkProbe;
    _timeDeadlineMs = millis() + Gsm::CCLK_TIMEOUT_MS;
    logger.log(LogModule::GsmController, "time cascade start server=%s tzq=%d\n",
               _ntpServer[0] ? _ntpServer : "(none)", (int)_ntpTzQuarters);
    return true;
}

bool GsmController::requestCclkProbe(int8_t tzOffsetHours) {
    if (_timeStep != TimeStep::Idle) return false;
    if (_state != GsmState::READY) return false;
    if (modemServiceEpochBusy()) return false;
    // Refuse during CIPSEND/connect — handleReady would skip serviceTimeSync and timeout.
    if (_stack.tcp.isTcpEpochBusy() || _stack.tcp.isBusBusy()) return false;
    if (_await != AwaitKind::NONE) return false;

    _ntpServer[0] = '\0';
    _ntpTzQuarters = gsm_time::tzHoursToQuarters(tzOffsetHours);
    _ntpUrcOk = false;
    _ntpUrcFail = false;
    _timeResultReady = false;
    _timeEpochUtc = 0;
    _timeSource = TimeSource::None;
    _timeTzHours = 0;
    _timeTzValid = false;
    _timeFailMask = 0;
    _timeFailReady = false;
    _timeCmdSent = false;
    _timeCclkOnly = true;
    _cclkSnap[0] = '\0';
    _cipgsmlocSnap[0] = '\0';
    _timeStep = TimeStep::CclkProbe;
    _timeDeadlineMs = millis() + Gsm::CCLK_TIMEOUT_MS;
    logger.log(LogModule::GsmController, "time CCLK-lite probe start tzq=%d\n", (int)_ntpTzQuarters);
    return true;
}

bool GsmController::takeTimeEpochUtc(time_t& epochUtcOut, TimeSource* sourceOut, int8_t* tzHoursOut,
                                     bool* tzValidOut) {
    if (!_timeResultReady) return false;
    epochUtcOut = _timeEpochUtc;
    if (sourceOut) *sourceOut = _timeSource;
    if (tzHoursOut) *tzHoursOut = _timeTzHours;
    if (tzValidOut) *tzValidOut = _timeTzValid;
    _timeResultReady = false;
    _timeEpochUtc = 0;
    _timeTzHours = 0;
    _timeTzValid = false;
    return true;
}

bool GsmController::takeTimeSyncFail(uint8_t& failMaskOut) {
    if (!_timeFailReady) return false;
    failMaskOut = _timeFailMask;
    _timeFailReady = false;
    _timeFailMask = 0;
    return true;
}

void GsmController::timeMarkMethodFail_(uint8_t bit) {
    _timeFailMask = static_cast<uint8_t>(_timeFailMask | bit);
}

void GsmController::timeFailStep_(const char* why) {
    if (_timeStep == TimeStep::Idle) return;
    if (_timeStep == TimeStep::CclkProbe) {
        timeMarkMethodFail_(TimeFailCclk);
    } else if (_timeStep == TimeStep::Cipshut || _timeStep == TimeStep::Cipgsmloc ||
               _timeStep == TimeStep::WaitCipgsmlocUrc) {
        timeMarkMethodFail_(TimeFailCipgsmloc);
    } else {
        timeMarkMethodFail_(TimeFailCntp);
    }
    timeFailTerminal_(why);
}

void GsmController::timeFailTerminal_(const char* why) {
    logger.log(LogModule::GsmController, "time cascade fail: %s mask=0x%02X\n", why ? why : "?",
               (unsigned)_timeFailMask);
    _timeFailReady = true;
    _timeResultReady = false;
    resetAwait();
    _timeStep = TimeStep::Idle;
    _timeCmdSent = false;
    _timeCclkOnly = false;
    _ntpUrcOk = false;
    _ntpUrcFail = false;
}

void GsmController::timeFinishOk_(time_t epochUtc, TimeSource src, bool tzValid, int8_t tzHours) {
    _timeEpochUtc = epochUtc;
    _timeSource = src;
    _timeTzValid = tzValid;
    _timeTzHours = tzValid ? tzHours : static_cast<int8_t>(0);
    _timeResultReady = true;
    _timeFailReady = false;
    _timeFailMask = 0;
    resetAwait();
    _timeStep = TimeStep::Idle;
    _timeCmdSent = false;
    _timeCclkOnly = false;
    const char* name = "?";
    switch (src) {
        case TimeSource::Cclk: name = "cclk"; break;
        case TimeSource::Cipgsmloc: name = "cipgsmloc"; break;
        case TimeSource::Cntp: name = "cntp"; break;
        default: break;
    }
    if (tzValid) {
        logger.log(LogModule::GsmController, "time ok source=%s epoch=%ld tz=%+dh\n", name, (long)epochUtc,
                   (int)tzHours);
    } else {
        logger.log(LogModule::GsmController, "time ok source=%s epoch=%ld\n", name, (long)epochUtc);
    }
}

void GsmController::timeAdvanceToCipgsmloc_(uint32_t now) {
    // Reset CIP stack after MQTT CIPCLOSE so CIPGSMLOC/CNTP see a clean IP session.
    resetAwait();
    clearResponse();
    _timeCmdSent = false;
    _cipgsmlocSnap[0] = '\0';
    _timeStep = TimeStep::Cipshut;
    _timeDeadlineMs = now + Sim800Tcp::CIPSHUT_TIMEOUT_MS;
    logger.log(LogModule::GsmController, "time → CIPSHUT then CIPGSMLOC\n");
}

void GsmController::timeEnterCipgsmlocCmd_(uint32_t now) {
    _stack.tcp.noteExternalCipShut();
    resetAwait();
    clearResponse();
    _timeCmdSent = false;
    _cipgsmlocSnap[0] = '\0';
    _timeStep = TimeStep::Cipgsmloc;
    _timeDeadlineMs = now + Gsm::CIPGSMLOC_TIMEOUT_MS;
    logger.log(LogModule::GsmController, "time → CIPGSMLOC\n");
}

void GsmController::timeAdvanceToCntpOrFail_(uint32_t now, const char* why) {
    if (!_ntpServer[0]) {
        timeFailTerminal_(why ? why : "no_ntp_server");
        return;
    }
    resetAwait();
    clearResponse();
    _timeCmdSent = false;
    _ntpUrcOk = false;
    _ntpUrcFail = false;
    _timeStep = TimeStep::Cntpcid;
    _timeDeadlineMs = now + Gsm::CNTP_AT_TIMEOUT_MS;
    logger.log(LogModule::GsmController, "time → CNTP (%s)\n", why ? why : "?");
}

void GsmController::timeSoftMissCclkLite_(const char* why) {
    logger.log(LogModule::GsmController, "time CCLK-lite miss: %s\n", why ? why : "?");
    resetAwait();
    _timeStep = TimeStep::Idle;
    _timeCmdSent = false;
    _timeCclkOnly = false;
    _timeFailReady = false;
    _timeResultReady = false;
}

void GsmController::serviceTimeSync(uint32_t now) {
    if (_timeStep == TimeStep::Idle) return;

    // Hard rule: never share the modem IP stack with CIP TCP (CIPGSMLOC/CNTP).
    // CCLK probe is AT-only and safe even if TCP is up.
    if (_timeStep != TimeStep::CclkProbe && tcpSocketActive()) {
        if (_timeStep == TimeStep::Cipshut || _timeStep == TimeStep::Cipgsmloc ||
            _timeStep == TimeStep::WaitCipgsmlocUrc) {
            timeMarkMethodFail_(TimeFailCipgsmloc);
        } else {
            timeMarkMethodFail_(TimeFailCntp);
        }
        timeFailTerminal_("tcp_taken");
        return;
    }
    if (_stack.tcp.isBusBusy()) return;

    auto advanceAfterOk = [&](TimeStep next, uint32_t timeoutMs) {
        resetAwait();
        clearResponse();
        _timeCmdSent = false;
        _timeStep = next;
        _timeDeadlineMs = now + timeoutMs;
    };

    auto failCntp = [&](const char* why) {
        timeMarkMethodFail_(TimeFailCntp);
        timeFailTerminal_(why);
    };

    auto finishCipgsmlocSnap_ = [&]() {
        time_t utc = 0;
        if (_cipgsmlocSnap[0] && gsm_time::parseCipgsmloc(_cipgsmlocSnap, utc)) {
            timeFinishOk_(utc, TimeSource::Cipgsmloc);
            return;
        }
        timeMarkMethodFail_(TimeFailCipgsmloc);
        timeAdvanceToCntpOrFail_(now, "CIPGSMLOC parse");
    };

    switch (_timeStep) {
    case TimeStep::CclkProbe:
        if (!_timeCmdSent) {
            _cclkSnap[0] = '\0';
            sendAt("AT+CCLK?", "CCLK", AwaitKind::OK, Gsm::CCLK_TIMEOUT_MS);
            _timeCmdSent = true;
            _timeDeadlineMs = now + Gsm::CCLK_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            if (_timeCclkOnly) {
                // Soft miss — do not sticky TIME_CCLK_FAIL during MQTT-up lite probes.
                timeSoftMissCclkLite_("cclk_only_at");
                return;
            }
            // Genuine AT fault — mark before fallback.
            timeMarkMethodFail_(TimeFailCclk);
            timeAdvanceToCipgsmloc_(now);
            return;
        }
        if (!_awaitOk) return;
        {
            gsm_time::CclkTime ct;
            if (readCclkSnapshot(_cclkSnap, ct)) {
                timeFinishOk_(ct.utc, TimeSource::Cclk, ct.tzWholeHours, ct.tzHours);
                return;
            }
            if (_timeCclkOnly) {
                timeSoftMissCclkLite_("stale");
                return;
            }
            // Expected path until NITZ fills the modem clock — do not set TIME_CCLK_FAIL.
            logger.log(LogModule::GsmController, "CCLK probe stale/unusable, next\n");
            timeAdvanceToCipgsmloc_(now);
        }
        return;

    case TimeStep::Cipshut:
        if (!_timeCmdSent) {
            // Tag TIMESHUT (not CIPSHUT) so Sim800TcpTransport::consumeAtResult does not swallow OK.
            sendAt("AT+CIPSHUT", "TIMESHUT", AwaitKind::OK, Sim800Tcp::CIPSHUT_TIMEOUT_MS);
            _timeCmdSent = true;
            _timeDeadlineMs = now + Sim800Tcp::CIPSHUT_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            logger.log(LogModule::GsmController, "time CIPSHUT fail/timeout — continue CIPGSMLOC\n");
            timeEnterCipgsmlocCmd_(now);
            return;
        }
        if (!_awaitOk) return;
        timeEnterCipgsmlocCmd_(now);
        return;

    case TimeStep::Cipgsmloc:
        if (!_timeCmdSent) {
            _cipgsmlocSnap[0] = '\0';
            sendAt("AT+CIPGSMLOC=2,1", "LOC", AwaitKind::OK, Gsm::CIPGSMLOC_TIMEOUT_MS);
            _timeCmdSent = true;
            _timeDeadlineMs = now + Gsm::CIPGSMLOC_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            timeMarkMethodFail_(TimeFailCipgsmloc);
            timeAdvanceToCntpOrFail_(now, "CIPGSMLOC fail");
            return;
        }
        if (!_awaitOk) return;
        // SIM800 often returns OK first, then +CIPGSMLOC — wait like WaitCntpUrc.
        resetAwait();
        clearResponse();
        _timeCmdSent = false;
        _timeStep = TimeStep::WaitCipgsmlocUrc;
        // Always grant a full URC grace after OK (do not inherit a near-zero accept deadline).
        _timeDeadlineMs = now + Gsm::CIPGSMLOC_URC_GRACE_MS;
        return;

    case TimeStep::WaitCipgsmlocUrc:
        if (_cipgsmlocSnap[0]) {
            finishCipgsmlocSnap_();
            return;
        }
        if ((int32_t)(now - _timeDeadlineMs) >= 0) {
            timeMarkMethodFail_(TimeFailCipgsmloc);
            timeAdvanceToCntpOrFail_(now, "CIPGSMLOC URC timeout");
            return;
        }
        return;

    case TimeStep::Cntpcid:
        if (!_timeCmdSent) {
            sendAt("AT+CNTPCID=1", "CNTP", AwaitKind::OK, Gsm::CNTP_AT_TIMEOUT_MS);
            _timeCmdSent = true;
            _timeDeadlineMs = now + Gsm::CNTP_AT_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            failCntp("CNTPCID");
            return;
        }
        if (!_awaitOk) return;
        advanceAfterOk(TimeStep::CntpSet, Gsm::CNTP_AT_TIMEOUT_MS);
        return;

    case TimeStep::CntpSet: {
        if (!_timeCmdSent) {
            char cmd[TextBytes::TimeCfg::NTP_SERVER + 24];
            snprintf(cmd, sizeof(cmd), "AT+CNTP=\"%s\",%d", _ntpServer, (int)_ntpTzQuarters);
            sendAt(cmd, "CNTP", AwaitKind::OK, Gsm::CNTP_AT_TIMEOUT_MS);
            _timeCmdSent = true;
            _timeDeadlineMs = now + Gsm::CNTP_AT_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            failCntp("CNTP set");
            return;
        }
        if (!_awaitOk) return;
        advanceAfterOk(TimeStep::CntpRun, Gsm::CNTP_AT_TIMEOUT_MS);
        return;
    }

    case TimeStep::CntpRun:
        if (!_timeCmdSent) {
            _ntpUrcOk = false;
            _ntpUrcFail = false;
            sendAt("AT+CNTP", "CNTP", AwaitKind::OK, Gsm::CNTP_AT_TIMEOUT_MS);
            _timeCmdSent = true;
            _timeDeadlineMs = now + Gsm::CNTP_AT_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            failCntp("CNTP run");
            return;
        }
        if (!_awaitOk) return;
        resetAwait();
        clearResponse();
        _timeCmdSent = false;
        _timeStep = TimeStep::WaitCntpUrc;
        _timeDeadlineMs = now + Gsm::CNTP_SYNC_TIMEOUT_MS;
        return;

    case TimeStep::WaitCntpUrc:
        if (_ntpUrcFail) {
            failCntp("CNTP URC fail");
            return;
        }
        if (_ntpUrcOk) {
            advanceAfterOk(TimeStep::CclkAfterCntp, Gsm::CCLK_TIMEOUT_MS);
            return;
        }
        if ((int32_t)(now - _timeDeadlineMs) >= 0) {
            failCntp("CNTP URC timeout");
            return;
        }
        return;

    case TimeStep::CclkAfterCntp:
        if (!_timeCmdSent) {
            _cclkSnap[0] = '\0';
            sendAt("AT+CCLK?", "CCLK", AwaitKind::OK, Gsm::CCLK_TIMEOUT_MS);
            _timeCmdSent = true;
            _timeDeadlineMs = now + Gsm::CCLK_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            failCntp("CCLK");
            return;
        }
        if (!_awaitOk) return;
        {
            gsm_time::CclkTime ct;
            if (!readCclkSnapshot(_cclkSnap, ct)) {
                failCntp("CCLK parse");
                return;
            }
            timeFinishOk_(ct.utc, TimeSource::Cntp, ct.tzWholeHours, ct.tzHours);
        }
        return;

    default:
        _timeStep = TimeStep::Idle;
        _timeCclkOnly = false;
        return;
    }
}
