/**
 * @file GsmController.Fsm.Gprs.cpp
 */
#include "gsm/GsmController.h"

#include "config/Config.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "common/Logger.h"

void GsmController::handleGprsSetup() {
    const uint32_t now = millis();
    if (_lastCommandTime == 0) {
        _gprsCfgStep = 0;
        // Профиль bearer SAPBR 1 (стек CIP с одним сокетом).
        sendAt("AT+SAPBR=3,1,\"Contype\",\"GPRS\"", nullptr, AwaitKind::OK, Gsm::APN_TIMEOUT_MS);
        return;
    }

    if (_awaitError) {
        _retryCount++;
        resetAwait();
        if (_retryCount >= Gsm::MAX_RETRIES) {
            changeState(GsmState::ERROR);
            core.getErrorManager().set(classifyBearerFail_());
            logger.log(LogModule::GsmController, "APN setup failed\n");
            return;
        }
        // Повтор текущего шага.
        _lastCommandTime = 0;
        return;
    }

    if (_awaitOk) {
        resetAwait();
        const auto& gsmCfg = config.getBase().gsm;
        char cmd[Gsm::CMD_BUFFER_SIZE];
        switch (_gprsCfgStep) {
            case 0:
                _gprsCfgStep = 1;
                snprintf(cmd, sizeof(cmd), "AT+SAPBR=3,1,\"APN\",\"%s\"", gsmCfg.apn);
                sendAt(cmd, nullptr, AwaitKind::OK, Gsm::APN_TIMEOUT_MS);
                return;
            case 1:
                _gprsCfgStep = 2;
                if (gsmCfg.apn_user[0] || gsmCfg.apn_pass[0]) {
                    snprintf(cmd, sizeof(cmd), "AT+SAPBR=3,1,\"USER\",\"%s\"", gsmCfg.apn_user);
                    if (!sendAt(cmd, nullptr, AwaitKind::OK, Gsm::APN_TIMEOUT_MS)) return;
                    return;
                }
                // проход дальше
            case 2:
                _gprsCfgStep = 3;
                if (gsmCfg.apn_pass[0]) {
                    snprintf(cmd, sizeof(cmd), "AT+SAPBR=3,1,\"PWD\",\"%s\"", gsmCfg.apn_pass);
                    if (!sendAt(cmd, nullptr, AwaitKind::OK, Gsm::APN_TIMEOUT_MS)) return;
                    return;
                }
                // проход дальше
            default:
                clearResponse();
                _retryCount = 0;
                snapshotAppliedApn_();
                changeState(GsmState::GPRS_ATTACH);
                return;
        }
    }

    if (awaitTimedOut(now)) {
        _retryCount++;
        if (_retryCount >= Gsm::MAX_RETRIES) {
            changeState(GsmState::ERROR);
            core.getErrorManager().set(classifyBearerFail_());
            logger.log(LogModule::GsmController, "APN setup failed\n");
            return;
        }
        // Повтор текущего шага.
        _lastCommandTime = 0;
    }
}

void GsmController::handleGprsAttach() {
    const uint32_t now = millis();

    // Сначала статус: тёплый модем / reattach уже может иметь открытый bearer с IP.
    // 0=опрос SAPBR=2,1 → 1=открытие SAPBR=1,1 → 2=повторный опрос после неудачи открытия.
    if (_lastCommandTime == 0) {
        if (_attachProbeStep == 0 || _attachProbeStep == 2) {
            sendAt("AT+SAPBR=2,1", nullptr, AwaitKind::IP, Gsm::GET_IP_TIMEOUT_MS);
            return;
        }
        sendAt("AT+SAPBR=1,1", nullptr, AwaitKind::OK, Gsm::PDP_ACTIVATE_TIMEOUT_MS);
        return;
    }

    if (_attachProbeStep == 0) {
        if (_awaitGotIp) {
            resetAwait();
            clearResponse();
            _retryCount = 0;
            core.getErrorManager().clear(ErrorCode::GSM_APN_FAIL);
            logger.log(LogModule::GsmController, "SAPBR already up (warm), open skipped\n");
            changeState(GsmState::READY);
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
            changeState(GsmState::GPRS_GETIP);
            return;
        }
        if (_awaitError || awaitTimedOut(now)) {
            // Открытие не удалось — часто «уже открыт»; проверить IP до ERROR.
            resetAwait();
            clearResponse();
            _attachProbeStep = 2;
            _lastCommandTime = 0;
            return;
        }
        return;
    }

    // _attachProbeStep == 2: повторный опрос после неудачи открытия
    if (_awaitGotIp) {
        resetAwait();
        clearResponse();
        _retryCount = 0;
        core.getErrorManager().clear(ErrorCode::GSM_APN_FAIL);
        logger.log(LogModule::GsmController, "SAPBR open failed, but IP present (already up)\n");
        changeState(GsmState::READY);
        updateSignalQuality();
        return;
    }
    if (_awaitOk || _awaitError || awaitTimedOut(now)) {
        _retryCount++;
        resetAwait();
        clearResponse();
        if (_retryCount >= Gsm::MAX_RETRIES) {
            changeState(GsmState::ERROR);
            core.getErrorManager().set(classifyBearerFail_());
            logger.log(LogModule::GsmController, "SAPBR open failed\n");
            return;
        }
        _attachProbeStep = 1;
        _lastCommandTime = 0;
    }
}

void GsmController::snapshotAppliedApn_() {
    const auto& g = config.getBase().gsm;
    strlcpy(_appliedApn, g.apn, sizeof(_appliedApn));
    strlcpy(_appliedApnUser, g.apn_user, sizeof(_appliedApnUser));
    strlcpy(_appliedApnPass, g.apn_pass, sizeof(_appliedApnPass));
    _apnAppliedValid = true;
}

void GsmController::clearAppliedApn_() {
    _apnAppliedValid = false;
    _appliedApn[0] = '\0';
    _appliedApnUser[0] = '\0';
    _appliedApnPass[0] = '\0';
}

bool GsmController::apnMatchesApplied_() const {
    if (!_apnAppliedValid) return false;
    const auto& g = config.getBase().gsm;
    return strcmp(_appliedApn, g.apn) == 0 && strcmp(_appliedApnUser, g.apn_user) == 0 &&
           strcmp(_appliedApnPass, g.apn_pass) == 0;
}

void GsmController::handleGprsGetIp() {
    const uint32_t now = millis();
    if (_lastCommandTime == 0) {
        sendAt("AT+SAPBR=2,1", nullptr, AwaitKind::IP, Gsm::GET_IP_TIMEOUT_MS);
        return;
    }

    if (_awaitError) {
        if (_stack.at.isBusy() || _stack.at.hasResult()) return;
        _retryCount++;
        resetAwait();
        if (_retryCount >= Gsm::MAX_RETRIES) {
            changeState(GsmState::ERROR);
            core.getErrorManager().set(classifyBearerFail_());
            logger.log(LogModule::GsmController, "failed to get IP\n");
            return;
        }
        sendAt("AT+SAPBR=2,1", nullptr, AwaitKind::IP, Gsm::GET_IP_TIMEOUT_MS);
        return;
    }

    if (_awaitGotIp) {
        clearResponse();
        _retryCount = 0;
        core.getErrorManager().clear(ErrorCode::GSM_APN_FAIL);
        changeState(GsmState::READY);
        // Разовая диагностика при входе в READY (неблокирующая): сразу запросить CSQ.
        updateSignalQuality();
        return;
    }

    if (awaitTimedOut(now)) {
        // Дедлайн GSM идёт с enqueue, AtSession — с startNext: не слать второй SAPBR поверх живого AT.
        if (_stack.at.isBusy() || _stack.at.hasResult()) return;
        _retryCount++;
        resetAwait();
        if (_retryCount >= Gsm::MAX_RETRIES) {
            changeState(GsmState::ERROR);
            core.getErrorManager().set(classifyBearerFail_());
            logger.log(LogModule::GsmController, "failed to get IP\n");
            return;
        }
        sendAt("AT+SAPBR=2,1", nullptr, AwaitKind::IP, Gsm::GET_IP_TIMEOUT_MS);
    }
}
