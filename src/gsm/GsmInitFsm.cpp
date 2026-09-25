/**
 * @file GsmInitFsm.cpp
 * @brief INIT под-FSM: PreCfun @ UART_BAUD, baud search, NV IPR, resume CREG/CGATT/SAPBR, modem policy.
 */
#include "gsm/GSMController.h"
#include "gsm/GsmInitFsm.h"

#include "gsm/GsmInitPhase.h"
#include "config/Config.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "common/Logger.h"

#include <stdio.h>
#include <string.h>

namespace {
constexpr uint8_t kBaudCandidateCount = GsmInitFsm::kBaudCandidateCount;
inline const uint32_t* baudCandidates() {
    static const uint32_t kTable[kBaudCandidateCount] = {
        GSM::UART_BAUD,
        57600,
        38400,
        19200,
        9600,
    };
    return kTable;
}

/// Порог эскалации hypothesis: PreCfun или baud search.
inline uint32_t hypEscalateAfterMs(bool didPreCfun) {
    return didPreCfun ? GSM::POST_CFUN_RETRY_MS : GSM::PRE_CFUN_AFTER_MS;
}
} // namespace

uint32_t GsmInitFsm::baudCandidateAt(uint8_t idx) {
    if (idx >= kBaudCandidateCount) return GSM::UART_BAUD;
    return baudCandidates()[idx];
}

/// Public INIT entry used by GSMController::update (friend can call private handleInit).
void GsmInitFsm::tick(GSMController& gsm) {
    gsm.handleInit();
}

/**
 * INIT body — kept as GSMController method for private-field access; defined in GsmInitFsm TU.
 */
void GSMController::handleInit() {
    const uint32_t now = millis();

    if (_waitFirstUntilMs != 0 && (int32_t)(now - _waitFirstUntilMs) < 0) return;
    if (_waitFirstUntilMs != 0) _waitFirstUntilMs = 0;

    if (_postLockQuietUntilMs != 0 && (int32_t)(now - _postLockQuietUntilMs) < 0) return;
    _postLockQuietUntilMs = 0;

    const bool HypothesisWindow =
        !_baudSearchActive && (_initPhase == GsmInitPhase::HypSendAt || _initPhase == GsmInitPhase::HypAwaitAt ||
                               _initPhase == GsmInitPhase::HypSendCgmi || _initPhase == GsmInitPhase::HypAwaitCgmi);

    if (HypothesisWindow && !_verifiedModemContactSinceStop && _firstAtFallbackStartMs != 0) {
        const uint32_t elapsed = now - _firstAtFallbackStartMs;
        if (!_didPreBaudSearchCfun && elapsed >= GSM::PRE_CFUN_AFTER_MS) {
            _stack.at.reset();
            resetAwait();
            _lastCommandTime = 0;
            _retryCount = 0;
            _initPhase = GsmInitPhase::PreCfunSend;
            return;
        }
        if (_didPreBaudSearchCfun && elapsed >= GSM::POST_CFUN_RETRY_MS) {
            _stack.at.reset();
            resetAwait();
            _lastCommandTime = 0;
            _retryCount = 0;
            _baudSearchActive = true;
            _baudSearchRound = 1;
            if (GSM::BAUD_SEARCH_MAX_PASSES > 0 &&
                _baudSearchRound > GSM::BAUD_SEARCH_MAX_PASSES) {
                changeState(GSMState::ERROR);
                core.getErrorManager().set(ErrorCode::GSM_NO_RESPONSE);
                logger.log("[GSMController] baud search: max rounds exceeded\n");
                return;
            }
            // UART_BAUD уже пробовали в hypothesis — начинаем со следующего кандидата.
            _baudSearchBaudIdx = (kBaudCandidateCount > 1) ? 1 : 0;
            logger.log("[GSMController] baud search round %u start (from %lu)\n",
                       (unsigned)_baudSearchRound,
                       (unsigned long)baudCandidates()[_baudSearchBaudIdx]);
            gsmOpenUart(baudCandidates()[_baudSearchBaudIdx]);
            _baudSearchDeadlineMs = now + GSM::BAUD_SEARCH_SETTLE_PER_BAUD_MS;
            _initPhase = GsmInitPhase::BsSettle;
            return;
        }
    }

    if (_baudSearchActive) {
        switch (_initPhase) {
        case GsmInitPhase::BsCooldown: {
            if ((int32_t)(now - _baudCooldownUntilMs) < 0) return;
            _baudSearchRound++;
            if (GSM::BAUD_SEARCH_MAX_PASSES > 0 &&
                _baudSearchRound > GSM::BAUD_SEARCH_MAX_PASSES) {
                changeState(GSMState::ERROR);
                core.getErrorManager().set(ErrorCode::GSM_NO_RESPONSE);
                logger.log("[GSMController] baud search: max rounds exceeded\n");
                return;
            }
            logger.log("[GSMController] baud search round %u start\n", (unsigned)_baudSearchRound);
            _baudSearchBaudIdx = 0;
            gsmOpenUart(GSM::UART_BAUD);
            _baudSearchDeadlineMs = now + GSM::UART_SETTLE_MS;
            _initPhase = GsmInitPhase::BsSettle;
            return;
        }
        case GsmInitPhase::BsSettle:
            if ((int32_t)(now - _baudSearchDeadlineMs) < 0) return;
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::BsSendAt;
            return;
        case GsmInitPhase::BsSendAt:
            if (_lastCommandTime == 0) {
                sendAt("AT", nullptr, AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS);
                _initPhase = GsmInitPhase::BsAwaitAt;
            }
            return;
        case GsmInitPhase::BsAwaitAt:
            if (_awaitError) {
                resetAwait();
                clearResponse();
                _baudSearchBaudIdx++;
                if (_baudSearchBaudIdx >= kBaudCandidateCount) {
                    gsmOpenUart(GSM::UART_BAUD);
                    _baudCooldownUntilMs = now + GSM::BAUD_SEARCH_ROUND_COOLDOWN_MS;
                    _initPhase = GsmInitPhase::BsCooldown;
                    _lastCommandTime = 0;
                    return;
                }
                gsmOpenUart(baudCandidates()[_baudSearchBaudIdx]);
                _baudSearchDeadlineMs = now + GSM::BAUD_SEARCH_SETTLE_PER_BAUD_MS;
                _initPhase = GsmInitPhase::BsSettle;
                _lastCommandTime = 0;
                return;
            }
            if (_awaitOk) {
                resetAwait();
                clearResponse();
                sendAt("AT+CGMI", "CGMI", AwaitKind::OK, GSM::MODEM_ID_VERIFY_TIMEOUT_MS);
                _initPhase = GsmInitPhase::BsAwaitCgmi;
                return;
            }
            if (awaitTimedOut(now)) {
                resetAwait();
                clearResponse();
                _baudSearchBaudIdx++;
                if (_baudSearchBaudIdx >= kBaudCandidateCount) {
                    gsmOpenUart(GSM::UART_BAUD);
                    _baudCooldownUntilMs = now + GSM::BAUD_SEARCH_ROUND_COOLDOWN_MS;
                    _initPhase = GsmInitPhase::BsCooldown;
                    _lastCommandTime = 0;
                    return;
                }
                gsmOpenUart(baudCandidates()[_baudSearchBaudIdx]);
                _baudSearchDeadlineMs = now + GSM::BAUD_SEARCH_SETTLE_PER_BAUD_MS;
                _initPhase = GsmInitPhase::BsSettle;
                _lastCommandTime = 0;
            }
            return;
        case GsmInitPhase::BsAwaitCgmi:
            if (_awaitError) {
                resetAwait();
                clearResponse();
                _baudSearchBaudIdx++;
                if (_baudSearchBaudIdx >= kBaudCandidateCount) {
                    gsmOpenUart(GSM::UART_BAUD);
                    _baudCooldownUntilMs = now + GSM::BAUD_SEARCH_ROUND_COOLDOWN_MS;
                    _initPhase = GsmInitPhase::BsCooldown;
                    _lastCommandTime = 0;
                    return;
                }
                gsmOpenUart(baudCandidates()[_baudSearchBaudIdx]);
                _baudSearchDeadlineMs = now + GSM::BAUD_SEARCH_SETTLE_PER_BAUD_MS;
                _initPhase = GsmInitPhase::BsSettle;
                _lastCommandTime = 0;
                return;
            }
            if (_awaitOk) {
                if (!gsmCgmiResponseManufacturerOk()) {
                    resetAwait();
                    clearResponse();
                    _baudSearchBaudIdx++;
                    if (_baudSearchBaudIdx >= kBaudCandidateCount) {
                        gsmOpenUart(GSM::UART_BAUD);
                        _baudCooldownUntilMs = now + GSM::BAUD_SEARCH_ROUND_COOLDOWN_MS;
                        _initPhase = GsmInitPhase::BsCooldown;
                        _lastCommandTime = 0;
                        return;
                    }
                    gsmOpenUart(baudCandidates()[_baudSearchBaudIdx]);
                    _baudSearchDeadlineMs = now + GSM::BAUD_SEARCH_SETTLE_PER_BAUD_MS;
                    _initPhase = GsmInitPhase::BsSettle;
                    _lastCommandTime = 0;
                    return;
                }
                resetAwait();
                clearResponse();
                _verifiedModemContactSinceStop = true;
                _baudSearchActive = false;
                _retryCount = 0;
                _lastCommandTime = 0;
#if defined(GSM_MODEM_ECHO) && (GSM_MODEM_ECHO != 0)
                _initPhase = GsmInitPhase::EarlyAteSend;
#else
                _initPhase = GsmInitPhase::DIprQ;
#endif
                return;
            }
            if (awaitTimedOut(now)) {
                resetAwait();
                clearResponse();
                _baudSearchBaudIdx++;
                if (_baudSearchBaudIdx >= kBaudCandidateCount) {
                    gsmOpenUart(GSM::UART_BAUD);
                    _baudCooldownUntilMs = now + GSM::BAUD_SEARCH_ROUND_COOLDOWN_MS;
                    _initPhase = GsmInitPhase::BsCooldown;
                    _lastCommandTime = 0;
                    return;
                }
                gsmOpenUart(baudCandidates()[_baudSearchBaudIdx]);
                _baudSearchDeadlineMs = now + GSM::BAUD_SEARCH_SETTLE_PER_BAUD_MS;
                _initPhase = GsmInitPhase::BsSettle;
                _lastCommandTime = 0;
            }
            return;
        default:
            _baudSearchActive = false;
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::HypSendAt;
            return;
        }
    }

    switch (_initPhase) {
    case GsmInitPhase::PreCfunSend:
        gsmNoteModemSoftReboot(true);
        gsmOpenUart(GSM::UART_BAUD);
        flushInput();
        logger.log("[GSMController] PreCfun: AT+CFUN=1,1 @ %lu\n", (unsigned long)GSM::UART_BAUD);
        sendAt("AT+CFUN=1,1", "CFUN", AwaitKind::NONE, 0);
        _baudSearchDeadlineMs = now + GSM::POST_CFUN_QUIET_MS;
        _initPhase = GsmInitPhase::PreCfunQuiet;
        return;
    case GsmInitPhase::PreCfunQuiet:
        if ((int32_t)(now - _baudSearchDeadlineMs) < 0) return;
        flushInput();
        _stack.at.reset();
        resetAwait();
        clearResponse();
        _lastCommandTime = 0;
        _retryCount = 0;
        _firstAtFallbackStartMs = 0;
        _hypNextAttemptMs = 0;
        _initPhase = GsmInitPhase::HypSendAt;
        return;
    case GsmInitPhase::HypSendAt:
        if ((int32_t)(now - _hypNextAttemptMs) < 0) return;
        if (_lastCommandTime == 0) {
            if (_firstAtFallbackStartMs == 0) _firstAtFallbackStartMs = now;
            sendAt("AT", nullptr, AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS);
            _initPhase = GsmInitPhase::HypAwaitAt;
        }
        return;
    case GsmInitPhase::HypAwaitAt:
        if (_awaitError) {
            _retryCount++;
            resetAwait();
            clearResponse();
            if (_firstAtFallbackStartMs != 0 &&
                (now - _firstAtFallbackStartMs) >= hypEscalateAfterMs(_didPreBaudSearchCfun)) {
                return;
            }
            if (_retryCount >= GSM::MAX_RETRIES) _retryCount = 0;
            _hypNextAttemptMs = now + GSM::HYP_RETRY_GAP_MS;
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::HypSendAt;
            return;
        }
        if (_awaitOk) {
            resetAwait();
            clearResponse();
            sendAt("AT+CGMI", "CGMI", AwaitKind::OK, GSM::MODEM_ID_VERIFY_TIMEOUT_MS);
            _initPhase = GsmInitPhase::HypAwaitCgmi;
            return;
        }
        if (awaitTimedOut(now)) {
            _retryCount++;
            resetAwait();
            clearResponse();
            if (_firstAtFallbackStartMs != 0 &&
                (now - _firstAtFallbackStartMs) >= hypEscalateAfterMs(_didPreBaudSearchCfun)) {
                return;
            }
            if (_retryCount >= GSM::MAX_RETRIES) _retryCount = 0;
            _hypNextAttemptMs = now + GSM::HYP_RETRY_GAP_MS;
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::HypSendAt;
        }
        return;
    case GsmInitPhase::HypAwaitCgmi:
        if (_awaitError) {
            _retryCount++;
            resetAwait();
            clearResponse();
            if (_firstAtFallbackStartMs != 0 &&
                (now - _firstAtFallbackStartMs) >= hypEscalateAfterMs(_didPreBaudSearchCfun)) {
                return;
            }
            if (_retryCount >= GSM::MAX_RETRIES) _retryCount = 0;
            _hypNextAttemptMs = now + GSM::HYP_RETRY_GAP_MS;
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::HypSendAt;
            return;
        }
        if (_awaitOk) {
            if (!gsmCgmiResponseManufacturerOk()) {
                _retryCount++;
                resetAwait();
                clearResponse();
                if (_firstAtFallbackStartMs != 0 &&
                    (now - _firstAtFallbackStartMs) >= hypEscalateAfterMs(_didPreBaudSearchCfun)) {
                    return;
                }
                if (_retryCount >= GSM::MAX_RETRIES) _retryCount = 0;
                _hypNextAttemptMs = now + GSM::HYP_RETRY_GAP_MS;
                _lastCommandTime = 0;
                _initPhase = GsmInitPhase::HypSendAt;
                return;
            }
            resetAwait();
            clearResponse();
            _verifiedModemContactSinceStop = true;
            _retryCount = 0;
            _hypNextAttemptMs = 0;
            _lastCommandTime = 0;
#if defined(GSM_MODEM_ECHO) && (GSM_MODEM_ECHO != 0)
            _initPhase = GsmInitPhase::EarlyAteSend;
#else
            _initPhase = GsmInitPhase::DIprQ;
#endif
            return;
        }
        if (awaitTimedOut(now)) {
            _retryCount++;
            resetAwait();
            clearResponse();
            if (_firstAtFallbackStartMs != 0 &&
                (now - _firstAtFallbackStartMs) >= hypEscalateAfterMs(_didPreBaudSearchCfun)) {
                return;
            }
            if (_retryCount >= GSM::MAX_RETRIES) _retryCount = 0;
            _hypNextAttemptMs = now + GSM::HYP_RETRY_GAP_MS;
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::HypSendAt;
        }
        return;
    case GsmInitPhase::EarlyAteSend:
        if (_lastCommandTime == 0) {
            sendAt("ATE1", "ECHO", AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS);
            _initPhase = GsmInitPhase::EarlyAteWait;
        }
        return;
    case GsmInitPhase::EarlyAteWait:
        if (_awaitError || awaitTimedOut(now)) {
            resetAwait();
            clearResponse();
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::DIprQ;
            return;
        }
        if (_awaitOk) {
            resetAwait();
            clearResponse();
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::DIprQ;
        }
        return;
    case GsmInitPhase::DIprQ:
        if (!_didIprNvProbeThisCycle && _lastCommandTime == 0) {
            sendAt("AT+IPR?", "IPRQ", AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS);
            _initPhase = GsmInitPhase::DIprWait;
            return;
        }
        if (!_didIprNvProbeThisCycle) return;
        _lastCommandTime = 0;
        _initPhase = GsmInitPhase::CCreg;
        return;
    case GsmInitPhase::DIprWait:
        if (_awaitError) {
            resetAwait();
            clearResponse();
            _didIprNvProbeThisCycle = true;
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::CCreg;
            return;
        }
        if (_awaitOk) {
            uint32_t iprNv = 0;
            const bool parsed = gsmParseStoredIprBaud(iprNv);
            resetAwait();
            clearResponse();
            _didIprNvProbeThisCycle = true;
            if (!parsed || iprNv != GSM::UART_BAUD) {
                char cmd[40];
                snprintf(cmd, sizeof(cmd), "AT+IPR=%lu", (unsigned long)GSM::UART_BAUD);
                sendAt(cmd, "IPRST", AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS);
                _initPhase = GsmInitPhase::DLockWaitIpr;
            } else {
                _lastCommandTime = 0;
                _initPhase = GsmInitPhase::CCreg;
            }
            return;
        }
        if (awaitTimedOut(now)) {
            resetAwait();
            clearResponse();
            _didIprNvProbeThisCycle = true;
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::CCreg;
        }
        return;
    case GsmInitPhase::DLockWaitIpr:
        if (_awaitError || awaitTimedOut(now)) {
            resetAwait();
            clearResponse();
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::CCreg;
            return;
        }
        if (_awaitOk) {
            resetAwait();
            clearResponse();
            sendAt("AT&W", "NVW", AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS);
            _initPhase = GsmInitPhase::DLockWaitW;
        }
        return;
    case GsmInitPhase::DLockWaitW:
        if (_awaitError || awaitTimedOut(now)) {
            resetAwait();
            clearResponse();
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::CCreg;
            return;
        }
        if (_awaitOk) {
            resetAwait();
            clearResponse();
            _serial->flush();
            gsmOpenUart(GSM::UART_BAUD);
            _stack.at.reset();
            _lastCommandTime = 0;
            sendAt("AT", nullptr, AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS);
            _initPhase = GsmInitPhase::DLockAwaitVerifyAt;
        }
        return;
    case GsmInitPhase::DLockAwaitVerifyAt:
        if (_awaitError || awaitTimedOut(now)) {
            resetAwait();
            clearResponse();
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::CCreg;
            return;
        }
        if (_awaitOk) {
            resetAwait();
            clearResponse();
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::CCreg;
        }
        return;
    case GsmInitPhase::CCreg:
        if (_lastCommandTime == 0) {
            sendAt("AT+CREG?", nullptr, AwaitKind::CREG, GSM::REG_TIMEOUT_MS);
            _initPhase = GsmInitPhase::CCregWait;
        }
        return;
    case GsmInitPhase::CCregWait:
        if (_awaitError) {
            resetAwait();
            clearResponse();
            _postResumeTarget = GSMState::REGISTERING;
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::ModAte;
            return;
        }
        if ((_awaitOk && _awaitGotCreg) || awaitTimedOut(now)) {
            (void)awaitTimedOut(now);
            resetAwait();
            clearResponse();
            _lastCommandTime = 0;
            _initCgattBusyRetries = 0;
            _initPhase = GsmInitPhase::CCgatt;
        }
        return;
    case GsmInitPhase::CCgatt:
        if (_lastCommandTime == 0) {
            sendAt("AT+CGATT?", nullptr, AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS);
            _initPhase = GsmInitPhase::CCgattWait;
        }
        return;
    case GsmInitPhase::CCgattWait:
        if (_awaitOk) {
            resetAwait();
            clearResponse();
            _initCgattBusyRetries = 0;
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::CSapbr;
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            resetAwait();
            clearResponse();
            if (_initCgattBusyRetries < GSM::INIT_CGATT_RETRY_MAX) {
                _initCgattBusyRetries++;
                logger.log("[GSMController] INIT AT+CGATT? fail (retry %u/%u), wait %lums\n",
                           (unsigned)_initCgattBusyRetries,
                           (unsigned)GSM::INIT_CGATT_RETRY_MAX,
                           (unsigned long)GSM::INIT_CGATT_RETRY_DELAY_MS);
                _lastCommandTime = now;
                _initPhase = GsmInitPhase::CCgattBackoff;
                return;
            }
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::CSapbr;
        }
        return;
    case GsmInitPhase::CCgattBackoff:
        if ((int32_t)(now - _lastCommandTime) < (int32_t)GSM::INIT_CGATT_RETRY_DELAY_MS) {
            return;
        }
        _lastCommandTime = 0;
        _initPhase = GsmInitPhase::CCgatt;
        return;
    case GsmInitPhase::CSapbr:
        if (_lastCommandTime == 0) {
            _resumeSapbrHadIp = false;
            sendAt("AT+SAPBR=2,1", nullptr, AwaitKind::IP, GSM::GET_IP_TIMEOUT_MS);
            _initPhase = GsmInitPhase::CSapbrWait;
        }
        return;
    case GsmInitPhase::CSapbrWait:
        if (_awaitError || awaitTimedOut(now)) {
            _resumeSapbrHadIp = false;
            resetAwait();
            clearResponse();
            _postResumeTarget = GSMState::REGISTERING;
            if ((_cregStat == 1 || _cregStat == 5)) {
                _postResumeTarget = GSMState::GPRS_SETUP;
            }
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::ModAte;
            return;
        }
        if (_awaitOk || _awaitGotIp) {
            _resumeSapbrHadIp = _awaitGotIp;
            resetAwait();
            clearResponse();
            const bool regOk = (_cregStat == 1 || _cregStat == 5);
            if (regOk && _resumeSapbrHadIp) {
                _postResumeTarget = GSMState::READY;
            } else if (regOk) {
                _postResumeTarget = GSMState::GPRS_SETUP;
            } else {
                _postResumeTarget = GSMState::REGISTERING;
            }
            _lastCommandTime = 0;
            _initPhase = GsmInitPhase::ModAte;
        }
        return;
    case GsmInitPhase::ModAte:
        if (_lastCommandTime == 0) {
#if defined(GSM_MODEM_ECHO) && (GSM_MODEM_ECHO != 0)
            sendAt("ATE1", "CFG", AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS);
#else
            sendAt("ATE0", "CFG", AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS);
#endif
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            resetAwait();
            clearResponse();
        } else if (!_awaitOk) {
            return;
        } else {
            resetAwait();
            clearResponse();
        }
        _lastCommandTime = 0;
        _initPhase = GsmInitPhase::ModCmee;
        return;
    case GsmInitPhase::ModCmee:
        if (_lastCommandTime == 0) {
            sendAt("AT+CMEE=2", "CFG", AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS);
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            resetAwait();
            clearResponse();
        } else if (!_awaitOk) {
            return;
        } else {
            resetAwait();
            clearResponse();
        }
        _lastCommandTime = 0;
        _initPhase = GsmInitPhase::ModClts;
        return;
    case GsmInitPhase::ModClts:
        if (_lastCommandTime == 0) {
            // Network time sync (NITZ) → modem CCLK; best-effort fallback if CNTP fails later.
            sendAt("AT+CLTS=1", "CFG", AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS);
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            resetAwait();
            clearResponse();
        } else if (!_awaitOk) {
            return;
        } else {
            resetAwait();
            clearResponse();
        }
        _lastCommandTime = 0;
        _initPhase = GsmInitPhase::ModCreg2;
        return;
    case GsmInitPhase::ModCreg2:
        if (_lastCommandTime == 0) {
            sendAt("AT+CREG=2", "CFG", AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS);
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            resetAwait();
            clearResponse();
        } else if (!_awaitOk) {
            return;
        } else {
            resetAwait();
            clearResponse();
        }
        _retryCount = 0;
        {
            const char* t = "UNKNOWN";
            switch (_postResumeTarget) {
            case GSMState::READY: t = "READY"; break;
            case GSMState::GPRS_SETUP: t = "GPRS_SETUP"; break;
            case GSMState::REGISTERING: t = "REGISTERING"; break;
            default: break;
            }
            logger.log("[GSMController] INIT OK -> %s\n", t);
        }
        changeState(_postResumeTarget);
        if (_postResumeTarget == GSMState::READY) {
            updateSignalQuality();
        }
        return;
    default:
        break;
    }
}
