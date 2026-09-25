/**
 * @file GSMController.Fsm.ReadyError.cpp
 */
#include "gsm/GSMController.h"

#include "config/Config.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "common/Logger.h"

void GSMController::handleReady() {
    if (_userRebootRequested) {
        // Controlled reboot from UI: close bearer (best-effort), then CFUN=1,1 and quiet wait.
        if (_rebootStep == 0) {
            _rebootStep = 1;
            logger.log("[GSMController] User requested modem reboot\n");
            sendAt("AT+SAPBR=0,1", "SAPBR", AwaitKind::OK, 8000);
            return;
        }
        if (_rebootStep == 1) {
            if (_awaitOk || _awaitError || awaitTimedOut(millis())) {
                resetAwait();
                sendAt("AT+CFUN=1,1", "CFUN", AwaitKind::NONE, 0);
                gsmNoteModemSoftReboot(true);
                _postLockQuietUntilMs = millis() + GSM::POST_CFUN_QUIET_MS;
                _userRebootRequested = false;
                _rebootStep = 0;
                // Restart bring-up after reboot.
                changeState(GSMState::INIT);
                return;
            }
            return;
        }
    }

    // TCP stack recover exhausted — probe/reattach bearer (status-first in GPRS_ATTACH).
    if (_stack.tcp.consumeNeedsBearerReattach()) {
        logger.log("[GSMController] TCP stack recover exhausted, requesting reattach\n");
        _reattachRequested = true;
    }

    if (_reattachRequested) {
        const uint32_t now = millis();
        if (_reattachCooldownUntilMs != 0 && (int32_t)(now - _reattachCooldownUntilMs) < 0) {
            // Still cooling down; keep request pending.
        } else {
            _reattachRequested = false;

            // Exponential-ish backoff to avoid thrashing bearer on unstable networks.
            if (_reattachBackoffStep < 6) _reattachBackoffStep++;
            uint32_t delayMs = 5000UL << (_reattachBackoffStep - 1); // 5s,10s,20s,40s,80s,160s
            if (delayMs > 180000UL) delayMs = 180000UL;

            // If signal is very low/unknown, be more gentle.
            const int16_t rssi = _signal;
            if (rssi < 6 || rssi == 99) {
                delayMs = (delayMs < 60000UL) ? 60000UL : delayMs;
            }

            _reattachCooldownUntilMs = now + delayMs;
            logger.log("[GSMController] Reattach requested (backoff=%us, rssi=%d)\n",
                       (unsigned)(delayMs / 1000UL), (int)rssi);
            changeState(GSMState::GPRS_ATTACH);
            return;
        }
    }

    // Resilience: treat PDP DEACT as bearer drop, but treat CLOSED as TCP-level drop.
    // CLOSED is common for servers and should not automatically thrash SAPBR/bearer.
    if (_pdpDeactSeen) {
        _pdpDeactSeen = false;
        logger.log("[GSMController] PDP DEACT seen (URC), re-attaching\n");
        _reattachRequested = true;
        changeState(GSMState::GPRS_ATTACH);
        return;
    }
    if (_closedSeen) {
        _closedSeen = false;
        const uint32_t now = millis();
        if (_tcpClosedFirstMs == 0 || (now - _tcpClosedFirstMs) > GSM::TCP_CLOSED_REATTACH_WINDOW_MS) {
            _tcpClosedFirstMs = now;
            _tcpClosedStreak = 1;
        } else if (_tcpClosedStreak < 255) {
            _tcpClosedStreak++;
        }
        logger.log("[GSMController] TCP CLOSED (URC) streak=%u\n", (unsigned)_tcpClosedStreak);
        if (_tcpClosedStreak >= GSM::TCP_CLOSED_REATTACH_THRESHOLD) {
            logger.log("[GSMController] TCP CLOSED threshold reached, re-attaching bearer\n");
            _reattachRequested = true;
            changeState(GSMState::GPRS_ATTACH);
            return;
        }
        // Otherwise: do nothing, MQTT/TCP will reconnect without SAPBR thrash.
    }

    // Diagnostics should be sparse: do not constantly poke the modem.
    // Never enqueue CSQ/COPS while TCP owns the AT bus (CIPSEND / CIPSTART / recover).
    // Prefer in-flight NTP over sparse CSQ/COPS.
    const uint32_t now = millis();
    if (!_stack.tcp.isBusBusy()) {
        if (_timeStep != TimeStep::Idle) {
            serviceTimeSync(now);
            return;
        }
        if (now - _lastDiagMs >= GSM::READY_SIGNAL_INTERVAL_MS) {
            _lastDiagMs = now;
            updateSignalQuality();
        }
        if (now - _lastOperatorMs >= GSM::READY_OPERATOR_INTERVAL_MS) {
            _lastOperatorMs = now;
            readOperator();
        }
    }
}

void GSMController::handleError() {
    const uint32_t now = millis();

    // Cooldown/backoff: do nothing until allowed time.
    if (_cooldownUntilMs != 0 && (int32_t)(now - _cooldownUntilMs) < 0) {
        return;
    }

    // If we scheduled a restart and cooldown is over, do it now.
    if (_restartPending) {
        _restartPending = false;
        begin();
        return;
    }

    // Bring-up timebox: escalate recovery if we are stuck too long.
    const uint32_t bringupAge = (now - _bringupStartMs);
    if (bringupAge > 300000UL && _recoveryLevel < 2) { // 5 min
        _recoveryLevel = 2;
        ev(21, _recoveryLevel);
    }

    if (_recoveryLevel == 0) {
        _recoveryLevel = 1;
        ev(21, _recoveryLevel);
    }

    // L1: cheap FSM restart.
    if (_recoveryLevel == 1) {
        logger.log("[GSMController] Recovering (level 1): restart FSM\n");
        _cooldownUntilMs = now + GSM::ERROR_RECOVERY_DELAY_MS;
        _recoveryLevel = 2;
        _restartPending = true;
        return;
    }

    // L2: reset TCP/IP stack (CIPSHUT), then restart FSM.
    if (_recoveryLevel == 2) {
        logger.log("[GSMController] Recovering (level 2): CIPSHUT\n");
        _stack.tcp.stop("recover");
        (void)_stack.at.enqueueHigh({ "AT+CIPSHUT", 10000, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPSHUT" });
        _cooldownUntilMs = now + 5000UL;
        _recoveryLevel = 3;
        _restartPending = true;
        ev(22);
        return;
    }

    // L3: bearer reset (SAPBR close) + restart.
    if (_recoveryLevel == 3) {
        logger.log("[GSMController] Recovering (level 3): SAPBR reset\n");
        (void)_stack.at.enqueueHigh({ "AT+SAPBR=0,1", 8000, atExpectMask(AtSession::Expect::Ok), nullptr, "SAPBR0" });
        _cooldownUntilMs = now + 8000UL;
        _recoveryLevel = 4;
        _restartPending = true;
        ev(23);
        return;
    }

    // L4: CFUN reset, then restart.
    logger.log("[GSMController] Recovering (level 4): CFUN reset\n");
    (void)_stack.at.enqueueHigh({ "AT+CFUN=1,1", 1000, atExpectMask(AtSession::Expect::AnyLine), nullptr, "CFUN" });
    gsmNoteModemSoftReboot(true);
    _cooldownUntilMs = now + GSM::POST_CFUN_QUIET_MS;
    _recoveryLevel = 1;
    _restartPending = true;
    ev(24);
}
