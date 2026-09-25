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
            if (!sendAt("AT+SAPBR=0,1", "SAPBR", AwaitKind::OK, 8000)) {
                _rebootStep = 0; // retry next tick
            }
            return;
        }
        if (_rebootStep == 1) {
            if (_awaitOk || _awaitError || awaitTimedOut(millis())) {
                resetAwait();
                (void)sendAt("AT+CFUN=1,1", "CFUN", AwaitKind::NONE, 0);
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

    // Config APN* changed while bearer still open — force Contype/APN rewrite via reattach.
    if (!_reattachRequested && _apnAppliedValid && !apnMatchesApplied_()) {
        logger.log("[GSMController] APN config changed, requesting reattach\n");
        _reattachRequested = true;
    }

    // Resilience: treat PDP DEACT as bearer drop, but treat CLOSED as TCP-level drop.
    // Both go through the same backoff path as requestReattach (no immediate GPRS_ATTACH).
    if (_pdpDeactSeen) {
        _pdpDeactSeen = false;
        logger.log("[GSMController] PDP DEACT seen (URC), requesting reattach\n");
        _reattachRequested = true;
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
            logger.log("[GSMController] TCP CLOSED threshold reached, requesting reattach\n");
            _reattachRequested = true;
        }
        // Otherwise: MQTT/TCP reconnects without SAPBR thrash.
    }

    if (_reattachRequested) {
        const uint32_t now = millis();
        if (_reattachCooldownUntilMs == 0 || (int32_t)(now - _reattachCooldownUntilMs) >= 0) {
            leaveReadyForReattach_("ready");
            return;
        }
        // Cooldown: stay READY, keep request pending, continue sparse diag below.
    }

    // Diagnostics: never poke CSQ/COPS while a TCP socket is up/connecting (shared UART framing).
    // Prefer in-flight NTP over sparse CSQ/COPS; NTP itself refuses while TCP socket is up.
    const uint32_t now = millis();
    if (!_stack.tcp.isTcpEpochBusy() && !tcpSocketActive()) {
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
        _errorRecoveryCmdPending = false;
        begin();
        return;
    }

    // Exhausted full L1–L4 cycles: sticky fail, rare retries until UI modem reboot / READY.
    if (_errorRecoveryCycles >= GSM::ERROR_RECOVERY_MAX_CYCLES) {
        if (core.getErrorManager().get() != ErrorCode::GSM_NO_RESPONSE) {
            core.getErrorManager().set(ErrorCode::GSM_NO_RESPONSE);
        }
        logger.log("[GSMController] Recovering exhausted (cycles=%u) — wait %us or UI reboot\n",
                   (unsigned)_errorRecoveryCycles,
                   (unsigned)(GSM::ERROR_RECOVERY_EXHAUSTED_MS / 1000UL));
        _cooldownUntilMs = now + GSM::ERROR_RECOVERY_EXHAUSTED_MS;
        _errorRecoveryCycles = 0; // allow another slow round after the long wait
        _recoveryLevel = 1;
        _restartPending = true;
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

    // L2: reset TCP/IP stack (CIPSHUT), wait for bus idle, then arm restart.
    if (_recoveryLevel == 2) {
        if (!_errorRecoveryCmdPending) {
            logger.log("[GSMController] Recovering (level 2): CIPSHUT\n");
            _stack.tcp.stop("recover");
            if (!_stack.at.enqueueHigh(
                    { "AT+CIPSHUT", 10000, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPSHUT" })) {
                return; // retry enqueue next tick
            }
            _errorRecoveryCmdPending = true;
            ev(22);
            return;
        }
        if (_stack.tcp.isBusBusy() || _stack.at.isBusy() || _stack.at.hasResult()) {
            return;
        }
        _errorRecoveryCmdPending = false;
        _cooldownUntilMs = now + 2000UL;
        _recoveryLevel = 3;
        _restartPending = true;
        return;
    }

    // L3: bearer reset (SAPBR close), wait for result, then arm restart.
    if (_recoveryLevel == 3) {
        if (!_errorRecoveryCmdPending) {
            logger.log("[GSMController] Recovering (level 3): SAPBR reset\n");
            if (!_stack.at.enqueueHigh(
                    { "AT+SAPBR=0,1", 8000, atExpectMask(AtSession::Expect::Ok), nullptr, "SAPBR0" })) {
                return;
            }
            beginAwait(AwaitKind::OK, 8000);
            _errorRecoveryCmdPending = true;
            ev(23);
            return;
        }
        if (!(_awaitOk || _awaitError || awaitTimedOut(now))) {
            return;
        }
        resetAwait();
        _errorRecoveryCmdPending = false;
        _cooldownUntilMs = now + 2000UL;
        _recoveryLevel = 4;
        _restartPending = true;
        return;
    }

    // L4: CFUN reset, then restart — counts as one full recovery cycle.
    logger.log("[GSMController] Recovering (level 4): CFUN reset (cycle %u/%u)\n",
               (unsigned)(_errorRecoveryCycles + 1), (unsigned)GSM::ERROR_RECOVERY_MAX_CYCLES);
    if (!_stack.at.enqueueHigh(
            { "AT+CFUN=1,1", 1000, atExpectMask(AtSession::Expect::AnyLine), nullptr, "CFUN" })) {
        return;
    }
    gsmNoteModemSoftReboot(true);
    if (_errorRecoveryCycles < 255) _errorRecoveryCycles++;
    _cooldownUntilMs = now + GSM::POST_CFUN_QUIET_MS;
    _recoveryLevel = 1;
    _restartPending = true;
    ev(24);
}
