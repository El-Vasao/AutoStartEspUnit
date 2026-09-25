/**
 * @file GSMController.Fsm.Gprs.cpp
 */
#include "gsm/GSMController.h"

#include "config/Config.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "common/Logger.h"

void GSMController::handleGprsSetup() {
    const uint32_t now = millis();
    if (_lastCommandTime == 0) {
        _gprsCfgStep = 0;
        // Use SAPBR bearer profile 1 (CIP single-socket stack).
        sendAt("AT+SAPBR=3,1,\"Contype\",\"GPRS\"", nullptr, AwaitKind::OK, GSM::APN_TIMEOUT_MS);
        return;
    }

    if (_awaitError) {
        _retryCount++;
        resetAwait();
        if (_retryCount >= GSM::MAX_RETRIES) {
            changeState(GSMState::ERROR);
            core.getErrorManager().set(classifyBearerFail_());
            logger.log("[GSMController] APN setup failed\n");
            return;
        }
        // Retry current step.
        _lastCommandTime = 0;
        return;
    }

    if (_awaitOk) {
        resetAwait();
        const auto& gsmCfg = config.getBase().gsm;
        char cmd[GSM::CMD_BUFFER_SIZE];
        switch (_gprsCfgStep) {
            case 0:
                _gprsCfgStep = 1;
                snprintf(cmd, sizeof(cmd), "AT+SAPBR=3,1,\"APN\",\"%s\"", gsmCfg.apn);
                sendAt(cmd, nullptr, AwaitKind::OK, GSM::APN_TIMEOUT_MS);
                return;
            case 1:
                _gprsCfgStep = 2;
                if (gsmCfg.apn_user[0]) {
                    snprintf(cmd, sizeof(cmd), "AT+SAPBR=3,1,\"USER\",\"%s\"", gsmCfg.apn_user);
                    sendAt(cmd, nullptr, AwaitKind::OK, GSM::APN_TIMEOUT_MS);
                    return;
                }
                // fallthrough
            case 2:
                _gprsCfgStep = 3;
                if (gsmCfg.apn_user[0]) {
                    snprintf(cmd, sizeof(cmd), "AT+SAPBR=3,1,\"PWD\",\"%s\"", gsmCfg.apn_pass);
                    sendAt(cmd, nullptr, AwaitKind::OK, GSM::APN_TIMEOUT_MS);
                    return;
                }
                // fallthrough
            default:
                clearResponse();
                _retryCount = 0;
                snapshotAppliedApn_();
                changeState(GSMState::GPRS_ATTACH);
                return;
        }
    }

    if (awaitTimedOut(now)) {
        _retryCount++;
        if (_retryCount >= GSM::MAX_RETRIES) {
            changeState(GSMState::ERROR);
            core.getErrorManager().set(classifyBearerFail_());
            logger.log("[GSMController] APN setup failed\n");
            return;
        }
        // Retry current step.
        _lastCommandTime = 0;
    }
}

void GSMController::handleGprsAttach() {
    const uint32_t now = millis();

    // Status-first: warm modem / reattach may already have an open bearer with IP.
    // 0=probe SAPBR=2,1 → 1=open SAPBR=1,1 → 2=re-probe after open fail.
    if (_lastCommandTime == 0) {
        if (_attachProbeStep == 0 || _attachProbeStep == 2) {
            sendAt("AT+SAPBR=2,1", nullptr, AwaitKind::IP, GSM::GET_IP_TIMEOUT_MS);
            return;
        }
        sendAt("AT+SAPBR=1,1", nullptr, AwaitKind::OK, GSM::PDP_ACTIVATE_TIMEOUT_MS);
        return;
    }

    if (_attachProbeStep == 0) {
        if (_awaitGotIp) {
            resetAwait();
            clearResponse();
            _retryCount = 0;
            if (core.getErrorManager().get() == ErrorCode::GSM_APN_FAIL) {
                core.getErrorManager().clear();
            }
            logger.log("[GSMController] SAPBR already up (warm), skip open\n");
            changeState(GSMState::READY);
            updateSignalQuality();
            return;
        }
        if (_awaitOk || _awaitError || awaitTimedOut(now)) {
            resetAwait();
            clearResponse();
            _attachProbeStep = 1;
            _lastCommandTime = 0;
            return;
        }
        return;
    }

    if (_attachProbeStep == 1) {
        if (_awaitOk) {
            clearResponse();
            _retryCount = 0;
            changeState(GSMState::GPRS_GETIP);
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            // Open failed — often "already open"; re-check IP before ERROR.
            resetAwait();
            clearResponse();
            _attachProbeStep = 2;
            _lastCommandTime = 0;
            return;
        }
        return;
    }

    // _attachProbeStep == 2: re-probe after open fail
    if (_awaitGotIp) {
        resetAwait();
        clearResponse();
        _retryCount = 0;
        if (core.getErrorManager().get() == ErrorCode::GSM_APN_FAIL) {
            core.getErrorManager().clear();
        }
        logger.log("[GSMController] SAPBR open failed but IP present (already up)\n");
        changeState(GSMState::READY);
        updateSignalQuality();
        return;
    }
    if (_awaitOk || _awaitError || awaitTimedOut(now)) {
        _retryCount++;
        resetAwait();
        clearResponse();
        if (_retryCount >= GSM::MAX_RETRIES) {
            changeState(GSMState::ERROR);
            core.getErrorManager().set(classifyBearerFail_());
            logger.log("[GSMController] SAPBR open failed\n");
            return;
        }
        _attachProbeStep = 1;
        _lastCommandTime = 0;
    }
}

void GSMController::snapshotAppliedApn_() {
    const auto& g = config.getBase().gsm;
    strlcpy(_appliedApn, g.apn, sizeof(_appliedApn));
    strlcpy(_appliedApnUser, g.apn_user, sizeof(_appliedApnUser));
    strlcpy(_appliedApnPass, g.apn_pass, sizeof(_appliedApnPass));
    _apnAppliedValid = true;
}

void GSMController::clearAppliedApn_() {
    _apnAppliedValid = false;
    _appliedApn[0] = '\0';
    _appliedApnUser[0] = '\0';
    _appliedApnPass[0] = '\0';
}

bool GSMController::apnMatchesApplied_() const {
    if (!_apnAppliedValid) return false;
    const auto& g = config.getBase().gsm;
    return strcmp(_appliedApn, g.apn) == 0 && strcmp(_appliedApnUser, g.apn_user) == 0 &&
           strcmp(_appliedApnPass, g.apn_pass) == 0;
}

void GSMController::handleGprsGetIp() {
    const uint32_t now = millis();
    if (_lastCommandTime == 0) {
        sendAt("AT+SAPBR=2,1", nullptr, AwaitKind::IP, GSM::GET_IP_TIMEOUT_MS);
        return;
    }

    if (_awaitError) {
        _retryCount++;
        resetAwait();
        if (_retryCount >= GSM::MAX_RETRIES) {
            changeState(GSMState::ERROR);
            core.getErrorManager().set(classifyBearerFail_());
            logger.log("[GSMController] Failed to get IP\n");
            return;
        }
        sendAt("AT+SAPBR=2,1", nullptr, AwaitKind::IP, GSM::GET_IP_TIMEOUT_MS);
        return;
    }

    if (_awaitGotIp) {
        clearResponse();
        _retryCount = 0;
        if (core.getErrorManager().get() == ErrorCode::GSM_APN_FAIL) {
            core.getErrorManager().clear();
        }
        changeState(GSMState::READY);
        // One-shot diagnostics at READY entry (non-blocking): request CSQ now.
        updateSignalQuality();
        return;
    }

    if (awaitTimedOut(now)) {
        _retryCount++;
        if (_retryCount >= GSM::MAX_RETRIES) {
            changeState(GSMState::ERROR);
            core.getErrorManager().set(classifyBearerFail_());
            logger.log("[GSMController] Failed to get IP\n");
            return;
        }
        sendAt("AT+SAPBR=2,1", nullptr, AwaitKind::IP, GSM::GET_IP_TIMEOUT_MS);
    }
}
