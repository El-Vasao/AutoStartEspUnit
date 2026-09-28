/**
 * @file GSMController.Ntp.cpp
 * @brief Non-blocking wall-clock cascade: CCLK/NITZ → CIPGSMLOC → CNTP → CCLK.
 */
#include "gsm/GSMController.h"

#include "common/Logger.h"
#include "common/Constants.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {

bool parseCclkToUtc(const char* line, time_t& utcOut, int8_t* tzHoursOut = nullptr,
                    bool* tzWholeHoursOut = nullptr) {
    // +CCLK: "yy/MM/dd,hh:mm:ss±zz"  zz = quarters of an hour from GMT
    if (tzHoursOut) *tzHoursOut = 0;
    if (tzWholeHoursOut) *tzWholeHoursOut = false;

    const char* p = strstr(line, "+CCLK:");
    if (!p) return false;
    p = strchr(p, '"');
    if (!p) return false;
    p++;

    int yy = 0, MM = 0, dd = 0, hh = 0, mm = 0, ss = 0, tzq = 0;
    char sign = '+';
    if (sscanf(p, "%2d/%2d/%2d,%2d:%2d:%2d%c%d", &yy, &MM, &dd, &hh, &mm, &ss, &sign, &tzq) < 8) {
        return false;
    }
    if (sign == '-') tzq = -tzq;
    else if (sign != '+') return false;

    if (MM < 1 || MM > 12 || dd < 1 || dd > 31) return false;
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 60) return false;

    struct tm t {};
    t.tm_year = yy + 100; // 2000+yy as years since 1900
    t.tm_mon = MM - 1;
    t.tm_mday = dd;
    t.tm_hour = hh;
    t.tm_min = mm;
    t.tm_sec = ss;
    t.tm_isdst = 0;

    char* oldTz = getenv("TZ");
    char oldBuf[32]{};
    if (oldTz) strlcpy(oldBuf, oldTz, sizeof(oldBuf));
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t asUtc = mktime(&t);
    if (oldBuf[0]) setenv("TZ", oldBuf, 1);
    else unsetenv("TZ");
    tzset();

    if (asUtc == (time_t)-1) return false;

    const int32_t offsetSec = (int32_t)tzq * 15 * 60;
    utcOut = asUtc - (time_t)offsetSec;
    if (utcOut <= 0) return false;

    // Config / UI store whole hours only; fractional NITZ zones stay out of tz_offset_hours.
    if ((tzq % 4) == 0) {
        const int hours = tzq / 4;
        if (hours >= -12 && hours <= 14) {
            if (tzHoursOut) *tzHoursOut = static_cast<int8_t>(hours);
            if (tzWholeHoursOut) *tzWholeHoursOut = true;
        }
    } else {
        logger.log("[GSMController] CCLK tzq=%d not whole hours; skip config TZ\n", tzq);
    }
    return true;
}

/// Reject factory/NITZ-missing clocks (e.g. 04/01/01) — same floor as TimeSyncManager.
bool cclkEpochSane(time_t utc) {
    return utc >= 1700000000L; // ~2023-11
}

bool parseCipgsmlocToUtc(const char* line, time_t& utcOut) {
    // +CIPGSMLOC: 0,yyyy/MM/dd,hh:mm:ss   (UTC per SIMCom / EasyElectronics)
    // +CIPGSMLOC: <err> on failure
    const char* p = strstr(line, "+CIPGSMLOC:");
    if (!p) return false;
    p += 11;
    while (*p == ' ') p++;

    int code = -1;
    int yyyy = 0, MM = 0, dd = 0, hh = 0, mm = 0, ss = 0;
    if (sscanf(p, "%d,%d/%d/%d,%d:%d:%d", &code, &yyyy, &MM, &dd, &hh, &mm, &ss) < 7) {
        return false;
    }
    if (code != 0) return false;
    if (yyyy < 2023 || MM < 1 || MM > 12 || dd < 1 || dd > 31) return false;
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 60) return false;

    struct tm t {};
    t.tm_year = yyyy - 1900;
    t.tm_mon = MM - 1;
    t.tm_mday = dd;
    t.tm_hour = hh;
    t.tm_min = mm;
    t.tm_sec = ss;
    t.tm_isdst = 0;

    char* oldTz = getenv("TZ");
    char oldBuf[32]{};
    if (oldTz) strlcpy(oldBuf, oldTz, sizeof(oldBuf));
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t asUtc = mktime(&t);
    if (oldBuf[0]) setenv("TZ", oldBuf, 1);
    else unsetenv("TZ");
    tzset();

    if (asUtc == (time_t)-1) return false;
    utcOut = asUtc;
    return utcOut >= 1700000000L;
}

int8_t tzHoursToQuarters(int8_t tzOffsetHours) {
    int q = static_cast<int>(tzOffsetHours) * 4;
    if (q > 48) q = 48;
    if (q < -48) q = -48;
    return (int8_t)q;
}

} // namespace

bool GSMController::requestTimeSync(const char* ntpServer, int8_t tzOffsetHours) {
    if (_timeStep != TimeStep::Idle) return false;
    if (_state != GSMState::READY) return false;
    if (tcpSocketActive() || _stack.tcp.isBusBusy()) return false;
    if (_await != AwaitKind::NONE) return false;

    if (ntpServer && ntpServer[0]) {
        strlcpy(_ntpServer, ntpServer, sizeof(_ntpServer));
    } else {
        _ntpServer[0] = '\0';
    }
    _ntpTzQuarters = tzHoursToQuarters(tzOffsetHours);
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
    _cclkSnap[0] = '\0';
    _cipgsmlocSnap[0] = '\0';
    _timeStep = TimeStep::CclkProbe;
    _timeDeadlineMs = millis() + GSM::CCLK_TIMEOUT_MS;
    logger.log("[GSMController] time cascade start server=%s tzq=%d\n",
               _ntpServer[0] ? _ntpServer : "(none)", (int)_ntpTzQuarters);
    return true;
}

bool GSMController::takeTimeEpochUtc(time_t& epochUtcOut, TimeSource* sourceOut, int8_t* tzHoursOut,
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

bool GSMController::takeTimeSyncFail(uint8_t& failMaskOut) {
    if (!_timeFailReady) return false;
    failMaskOut = _timeFailMask;
    _timeFailReady = false;
    _timeFailMask = 0;
    return true;
}

void GSMController::timeMarkMethodFail_(uint8_t bit) {
    _timeFailMask = static_cast<uint8_t>(_timeFailMask | bit);
}

void GSMController::timeFailStep_(const char* why) {
    if (_timeStep == TimeStep::Idle) return;
    if (_timeStep == TimeStep::CclkProbe) {
        timeMarkMethodFail_(TimeFailCclk);
    } else if (_timeStep == TimeStep::Cipgsmloc) {
        timeMarkMethodFail_(TimeFailCipgsmloc);
    } else {
        timeMarkMethodFail_(TimeFailCntp);
    }
    timeFailTerminal_(why);
}

void GSMController::timeFailTerminal_(const char* why) {
    logger.log("[GSMController] time cascade fail: %s mask=0x%02X\n", why ? why : "?",
               (unsigned)_timeFailMask);
    _timeFailReady = true;
    _timeResultReady = false;
    resetAwait();
    _timeStep = TimeStep::Idle;
    _timeCmdSent = false;
    _ntpUrcOk = false;
    _ntpUrcFail = false;
}

void GSMController::timeFinishOk_(time_t epochUtc, TimeSource src, bool tzValid, int8_t tzHours) {
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
    const char* name = "?";
    switch (src) {
        case TimeSource::Cclk: name = "cclk"; break;
        case TimeSource::Cipgsmloc: name = "cipgsmloc"; break;
        case TimeSource::Cntp: name = "cntp"; break;
        default: break;
    }
    if (tzValid) {
        logger.log("[GSMController] time ok source=%s epoch=%ld tz=%+dh\n", name, (long)epochUtc,
                   (int)tzHours);
    } else {
        logger.log("[GSMController] time ok source=%s epoch=%ld\n", name, (long)epochUtc);
    }
}

void GSMController::timeAdvanceToCipgsmloc_(uint32_t now) {
    resetAwait();
    clearResponse();
    _timeCmdSent = false;
    _cipgsmlocSnap[0] = '\0';
    _timeStep = TimeStep::Cipgsmloc;
    _timeDeadlineMs = now + GSM::CIPGSMLOC_TIMEOUT_MS;
    logger.log("[GSMController] time → CIPGSMLOC\n");
}

void GSMController::timeAdvanceToCntpOrFail_(uint32_t now, const char* why) {
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
    _timeDeadlineMs = now + GSM::CNTP_AT_TIMEOUT_MS;
    logger.log("[GSMController] time → CNTP (%s)\n", why ? why : "?");
}

void GSMController::serviceTimeSync(uint32_t now) {
    if (_timeStep == TimeStep::Idle) return;

    // Hard rule: never share the modem IP stack with CIP TCP (CIPGSMLOC/CNTP).
    // CCLK probe is AT-only and safe even if TCP is up, but cascade starts only when TCP down.
    if (_timeStep != TimeStep::CclkProbe && tcpSocketActive()) {
        if (_timeStep == TimeStep::Cipgsmloc) {
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

    switch (_timeStep) {
    case TimeStep::CclkProbe:
        if (!_timeCmdSent) {
            _cclkSnap[0] = '\0';
            sendAt("AT+CCLK?", "CCLK", AwaitKind::OK, GSM::CCLK_TIMEOUT_MS);
            _timeCmdSent = true;
            _timeDeadlineMs = now + GSM::CCLK_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            timeMarkMethodFail_(TimeFailCclk);
            timeAdvanceToCipgsmloc_(now);
            return;
        }
        if (!_awaitOk) return;
        {
            time_t utc = 0;
            int8_t tzH = 0;
            bool tzOk = false;
            if (_cclkSnap[0] && parseCclkToUtc(_cclkSnap, utc, &tzH, &tzOk) && cclkEpochSane(utc)) {
                timeFinishOk_(utc, TimeSource::Cclk, tzOk, tzH);
                return;
            }
            logger.log("[GSMController] CCLK probe stale/unusable, next\n");
            timeMarkMethodFail_(TimeFailCclk);
            timeAdvanceToCipgsmloc_(now);
        }
        return;

    case TimeStep::Cipgsmloc:
        if (!_timeCmdSent) {
            _cipgsmlocSnap[0] = '\0';
            sendAt("AT+CIPGSMLOC=2,1", "LOC", AwaitKind::OK, GSM::CIPGSMLOC_TIMEOUT_MS);
            _timeCmdSent = true;
            _timeDeadlineMs = now + GSM::CIPGSMLOC_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            timeMarkMethodFail_(TimeFailCipgsmloc);
            timeAdvanceToCntpOrFail_(now, "CIPGSMLOC fail");
            return;
        }
        if (!_awaitOk) return;
        {
            time_t utc = 0;
            if (_cipgsmlocSnap[0] && parseCipgsmlocToUtc(_cipgsmlocSnap, utc)) {
                timeFinishOk_(utc, TimeSource::Cipgsmloc);
                return;
            }
            timeMarkMethodFail_(TimeFailCipgsmloc);
            timeAdvanceToCntpOrFail_(now, "CIPGSMLOC parse");
        }
        return;

    case TimeStep::Cntpcid:
        if (!_timeCmdSent) {
            sendAt("AT+CNTPCID=1", "CNTP", AwaitKind::OK, GSM::CNTP_AT_TIMEOUT_MS);
            _timeCmdSent = true;
            _timeDeadlineMs = now + GSM::CNTP_AT_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            failCntp("CNTPCID");
            return;
        }
        if (!_awaitOk) return;
        advanceAfterOk(TimeStep::CntpSet, GSM::CNTP_AT_TIMEOUT_MS);
        return;

    case TimeStep::CntpSet: {
        if (!_timeCmdSent) {
            char cmd[TextBytes::TimeCfg::NTP_SERVER + 24];
            snprintf(cmd, sizeof(cmd), "AT+CNTP=\"%s\",%d", _ntpServer, (int)_ntpTzQuarters);
            sendAt(cmd, "CNTP", AwaitKind::OK, GSM::CNTP_AT_TIMEOUT_MS);
            _timeCmdSent = true;
            _timeDeadlineMs = now + GSM::CNTP_AT_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            failCntp("CNTP set");
            return;
        }
        if (!_awaitOk) return;
        advanceAfterOk(TimeStep::CntpRun, GSM::CNTP_AT_TIMEOUT_MS);
        return;
    }

    case TimeStep::CntpRun:
        if (!_timeCmdSent) {
            _ntpUrcOk = false;
            _ntpUrcFail = false;
            sendAt("AT+CNTP", "CNTP", AwaitKind::OK, GSM::CNTP_AT_TIMEOUT_MS);
            _timeCmdSent = true;
            _timeDeadlineMs = now + GSM::CNTP_AT_TIMEOUT_MS;
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
        _timeDeadlineMs = now + GSM::CNTP_SYNC_TIMEOUT_MS;
        return;

    case TimeStep::WaitCntpUrc:
        if (_ntpUrcFail) {
            failCntp("CNTP URC fail");
            return;
        }
        if (_ntpUrcOk) {
            advanceAfterOk(TimeStep::CclkAfterCntp, GSM::CCLK_TIMEOUT_MS);
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
            sendAt("AT+CCLK?", "CCLK", AwaitKind::OK, GSM::CCLK_TIMEOUT_MS);
            _timeCmdSent = true;
            _timeDeadlineMs = now + GSM::CCLK_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            failCntp("CCLK");
            return;
        }
        if (!_awaitOk) return;
        {
            time_t utc = 0;
            int8_t tzH = 0;
            bool tzOk = false;
            if (_cclkSnap[0] == '\0' || !parseCclkToUtc(_cclkSnap, utc, &tzH, &tzOk) ||
                !cclkEpochSane(utc)) {
                failCntp("CCLK parse");
                return;
            }
            timeFinishOk_(utc, TimeSource::Cntp, tzOk, tzH);
        }
        return;

    default:
        _timeStep = TimeStep::Idle;
        return;
    }
}
