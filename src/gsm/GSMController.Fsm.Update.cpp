/**
 * @file GSMController.Fsm.Update.cpp
 * @brief Диспетчер update(), мост AtSession → await-флаги, редкий stall watchdog.
 */
#include "gsm/GSMController.h"

#include "gsm/GsmInitPhase.h"
#include "gsm/GsmInitFsm.h"
#include "common/Logger.h"

#include <stdint.h>

uint32_t GSMController::gsmStallFingerprint() const {
    uint32_t fp = (uint32_t)_state << 28;
    fp |= (uint32_t)(uint8_t)_initPhase << 20;
    fp |= (uint32_t)(uint8_t)_await << 16;
    fp |= (_awaitOk ? 1u : 0u) << 12;
    fp |= (_awaitError ? 1u : 0u) << 11;
    fp |= (_awaitGotCreg ? 1u : 0u) << 10;
    fp |= (_awaitGotIp ? 1u : 0u) << 9;
    fp ^= (uint32_t)_cmdId + ((uint32_t)_awaitCmdId << 16);
    fp ^= _lastCommandTime;
    return fp;
}

void GSMController::gsmMaybeLogStallWatchdog(uint32_t now) {
    if (_state == GSMState::IDLE || _state == GSMState::READY) return;

    const uint32_t fp = gsmStallFingerprint();
    if (fp != _gsmStallPrevFp) {
        _gsmStallPrevFp = fp;
        _gsmStallFpSinceMs = now;
        return;
    }

    constexpr uint32_t kIntervalMs = 8000;
    if ((int32_t)(now - _gsmStallFpSinceMs) < (int32_t)kIntervalMs) return;
    if ((int32_t)(now - _gsmStallLastLogMs) < (int32_t)kIntervalMs) return;

    _gsmStallLastLogMs = now;

    const char* awaitStr = "?";
    switch (_await) {
    case AwaitKind::NONE: awaitStr = "NONE"; break;
    case AwaitKind::OK: awaitStr = "OK"; break;
    case AwaitKind::CREG: awaitStr = "CREG"; break;
    case AwaitKind::IP: awaitStr = "IP"; break;
    }

    const char* phaseStr = (_state == GSMState::INIT) ? gsmInitPhaseName(_initPhase) : "-";

    logger.log(
        "[GSMController] stall: state=%s init=%s await=%s ok=%d err=%d creg=%d ip=%d "
        "cmd=%u acmd=%u stallMs=%lu at=\"%.48s\"\n",
        getStateString(),
        phaseStr,
        awaitStr,
        (int)_awaitOk,
        (int)_awaitError,
        (int)_awaitGotCreg,
        (int)_awaitGotIp,
        (unsigned)_cmdId,
        (unsigned)_awaitCmdId,
        (unsigned long)(now - _gsmStallFpSinceMs),
        _atCmdBuf);
}

void GSMController::drainAtResult_() {
    if (!_stack.at.hasResult()) return;
    const AtSession::Result r = _stack.at.takeResult();
    if (!_stack.tcp.consumeAtResult(r)) {
        gsmAbsorbAtSessionResult(r);
    }
}

void GSMController::gsmAbsorbAtSessionResult(const AtSession::Result& r) {
    // RX: ModemUart вызывает AtSession::onLine до onRxLine (GSMController.Core).
    // Ошибка/таймаут очереди AT — общий сигнал срыва; OK для Expect::Ok совпадает со строкой OK в handleAwaitLine.
    // _awaitGotCreg / _awaitGotIp ставит только handleAwaitLine (+CREG / SAPBR / IPv4).
    if (r.ok) {
        _awaitOk = true;
    }
    if (r.error || r.timedOut) {
        _awaitError = true;
    }
}

void GSMController::update() {
    _stack.uart.pollRx();
    _stack.at.tick(millis());
    const uint32_t now = millis();
    if (_state == GSMState::READY || _stack.tcp.isConnected() || _stack.tcp.isConnecting() ||
        _stack.tcp.isBusBusy()) {
        _stack.tcp.tick(now);
    }

    drainAtResult_();

    switch (_state) {
    case GSMState::IDLE:
        handleIdle();
        break;
    case GSMState::INIT:
        GsmInitFsm::tick(*this);
        break;
    case GSMState::REGISTERING:
        handleRegistering();
        break;
    case GSMState::GPRS_SETUP:
        handleGprsSetup();
        break;
    case GSMState::GPRS_ATTACH:
        handleGprsAttach();
        break;
    case GSMState::GPRS_GETIP:
        handleGprsGetIp();
        break;
    case GSMState::READY:
        handleReady();
        break;
    case GSMState::ERROR:
        handleError();
        break;
    }

    gsmMaybeLogStallWatchdog(now);
}

void GSMController::handleIdle() {}
