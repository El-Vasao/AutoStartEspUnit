/**
 * @file GSMController.Ntp.cpp
 * @brief Non-blocking SIM800 NTP: AT+CNTPCID / AT+CNTP / +CNTP URC / AT+CCLK?
 */
#include "gsm/GSMController.h"

#include "common/Logger.h"
#include "common/Constants.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {

bool parseCclkToUtc(const char* line, time_t& utcOut) {
    // +CCLK: "yy/MM/dd,hh:mm:ss±zz"  zz = quarters of an hour from GMT
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
    return utcOut > 0;
}

int8_t tzHoursToQuarters(int8_t tzOffsetHours) {
    int q = static_cast<int>(tzOffsetHours) * 4;
    if (q > 48) q = 48;
    if (q < -48) q = -48;
    return (int8_t)q;
}

} // namespace

bool GSMController::requestNtpSync(const char* ntpServer, int8_t tzOffsetHours) {
    if (_ntpStep != NtpStep::Idle) return false;
    if (_state != GSMState::READY) return false;
    if (tcpSocketActive() || _stack.tcp.isBusBusy()) return false;
    if (_await != AwaitKind::NONE) return false;
    if (!ntpServer || !ntpServer[0]) return false;

    strlcpy(_ntpServer, ntpServer, sizeof(_ntpServer));
    _ntpTzQuarters = tzHoursToQuarters(tzOffsetHours);
    _ntpUrcOk = false;
    _ntpUrcFail = false;
    _ntpResultReady = false;
    _ntpEpochUtc = 0;
    _ntpCmdSent = false;
    _ntpStep = NtpStep::Cntpcid;
    _ntpDeadlineMs = millis() + GSM::CNTP_AT_TIMEOUT_MS;
    logger.log("[GSMController] NTP sync start server=%s tzq=%d\n", _ntpServer, (int)_ntpTzQuarters);
    return true;
}

bool GSMController::takeNtpEpochUtc(time_t& epochUtcOut) {
    if (!_ntpResultReady) return false;
    epochUtcOut = _ntpEpochUtc;
    _ntpResultReady = false;
    _ntpEpochUtc = 0;
    return true;
}

void GSMController::ntpFail_(const char* why) {
    logger.log("[GSMController] NTP fail: %s\n", why ? why : "?");
    resetAwait();
    _ntpStep = NtpStep::Idle;
    _ntpCmdSent = false;
    _ntpUrcOk = false;
    _ntpUrcFail = false;
}

void GSMController::ntpFinishOk_(time_t epochUtc) {
    _ntpEpochUtc = epochUtc;
    _ntpResultReady = true;
    resetAwait();
    _ntpStep = NtpStep::Idle;
    _ntpCmdSent = false;
    logger.log("[GSMController] NTP ok epoch=%ld\n", (long)epochUtc);
}

void GSMController::serviceNtpSync(uint32_t now) {
    if (_ntpStep == NtpStep::Idle) return;

    // Hard rule: never share the modem IP stack with CIP TCP.
    if (tcpSocketActive()) {
        ntpFail_("tcp_taken");
        return;
    }
    if (_stack.tcp.isBusBusy()) return;

    auto advanceAfterOk = [&](NtpStep next, uint32_t timeoutMs) {
        resetAwait();
        clearResponse();
        _ntpCmdSent = false;
        _ntpStep = next;
        _ntpDeadlineMs = now + timeoutMs;
    };

    switch (_ntpStep) {
    case NtpStep::Cntpcid:
        if (!_ntpCmdSent) {
            sendAt("AT+CNTPCID=1", "CNTP", AwaitKind::OK, GSM::CNTP_AT_TIMEOUT_MS);
            _ntpCmdSent = true;
            _ntpDeadlineMs = now + GSM::CNTP_AT_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            ntpFail_("CNTPCID");
            return;
        }
        if (!_awaitOk) return;
        advanceAfterOk(NtpStep::CntpSet, GSM::CNTP_AT_TIMEOUT_MS);
        return;

    case NtpStep::CntpSet: {
        if (!_ntpCmdSent) {
            char cmd[TextBytes::TimeCfg::NTP_SERVER + 24];
            snprintf(cmd, sizeof(cmd), "AT+CNTP=\"%s\",%d", _ntpServer, (int)_ntpTzQuarters);
            sendAt(cmd, "CNTP", AwaitKind::OK, GSM::CNTP_AT_TIMEOUT_MS);
            _ntpCmdSent = true;
            _ntpDeadlineMs = now + GSM::CNTP_AT_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            ntpFail_("CNTP set");
            return;
        }
        if (!_awaitOk) return;
        advanceAfterOk(NtpStep::CntpRun, GSM::CNTP_AT_TIMEOUT_MS);
        return;
    }

    case NtpStep::CntpRun:
        if (!_ntpCmdSent) {
            _ntpUrcOk = false;
            _ntpUrcFail = false;
            sendAt("AT+CNTP", "CNTP", AwaitKind::OK, GSM::CNTP_AT_TIMEOUT_MS);
            _ntpCmdSent = true;
            _ntpDeadlineMs = now + GSM::CNTP_AT_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            ntpFail_("CNTP run");
            return;
        }
        if (!_awaitOk) return;
        resetAwait();
        clearResponse();
        _ntpCmdSent = false;
        _ntpStep = NtpStep::WaitCntpUrc;
        _ntpDeadlineMs = now + GSM::CNTP_SYNC_TIMEOUT_MS;
        return;

    case NtpStep::WaitCntpUrc:
        if (_ntpUrcFail) {
            ntpFail_("CNTP URC fail");
            return;
        }
        if (_ntpUrcOk) {
            advanceAfterOk(NtpStep::Cclk, GSM::CCLK_TIMEOUT_MS);
            return;
        }
        if ((int32_t)(now - _ntpDeadlineMs) >= 0) {
            ntpFail_("CNTP URC timeout");
            return;
        }
        return;

    case NtpStep::Cclk:
        if (!_ntpCmdSent) {
            _cclkSnap[0] = '\0';
            sendAt("AT+CCLK?", "CCLK", AwaitKind::OK, GSM::CCLK_TIMEOUT_MS);
            _ntpCmdSent = true;
            _ntpDeadlineMs = now + GSM::CCLK_TIMEOUT_MS;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            ntpFail_("CCLK");
            return;
        }
        if (!_awaitOk) return;
        {
            time_t utc = 0;
            if (_cclkSnap[0] == '\0' || !parseCclkToUtc(_cclkSnap, utc)) {
                ntpFail_("CCLK parse");
                return;
            }
            ntpFinishOk_(utc);
        }
        return;

    default:
        _ntpStep = NtpStep::Idle;
        return;
    }
}
