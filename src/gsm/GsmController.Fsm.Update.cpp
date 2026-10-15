/**
 * @file GsmController.Fsm.Update.cpp
 * @brief Диспетчер update(), мост AtSession → await-флаги, редкий stall watchdog.
 */
#include "gsm/GsmController.h"

#include "gsm/GsmInitPhase.h"
#include "common/Constants.h"
#include "common/Logger.h"

#include <stdint.h>

uint32_t GsmController::gsmStallFingerprint() const {
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

void GsmController::gsmMaybeLogStallWatchdog(uint32_t now) {
    if (_state == GsmState::IDLE || _state == GsmState::READY) return;

    const uint32_t fp = gsmStallFingerprint();
    if (fp != _gsmStallPrevFp) {
        _gsmStallPrevFp = fp;
        _gsmStallFpSinceMs = now;
        return;
    }

    if ((int32_t)(now - _gsmStallFpSinceMs) < (int32_t)Gsm::STALL_LOG_INTERVAL_MS) return;
    if ((int32_t)(now - _gsmStallLastLogMs) < (int32_t)Gsm::STALL_LOG_INTERVAL_MS) return;

    _gsmStallLastLogMs = now;

    const char* awaitStr = "?";
    switch (_await) {
    case AwaitKind::NONE: awaitStr = "NONE"; break;
    case AwaitKind::OK: awaitStr = "OK"; break;
    case AwaitKind::CREG: awaitStr = "CREG"; break;
    case AwaitKind::IP: awaitStr = "IP"; break;
    }

    const char* phaseStr = (_state == GsmState::INIT) ? gsmInitPhaseName(_initPhase) : "-";

    logger.log(LogModule::GsmController,
        "stall: state=%s init=%s await=%s ok=%d err=%d creg=%d ip=%d "
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

void GsmController::drainAtResult_() {
    if (!_stack.at.hasResult()) return;
    const AtSession::Result r = _stack.at.takeResult();
    if (_voicePhase == VoicePhase::CmgsWaitPrompt && r.gotPrompt) {
        _voicePromptSeen = true;
        return;
    }
    if (_voicePhase == VoicePhase::CmgsWaitPrompt && (r.error || r.timedOut)) {
        _awaitError = true;
        return;
    }
    if (!_stack.tcp.consumeAtResult(r)) {
        gsmAbsorbAtSessionResult(r);
    }
}

void GsmController::gsmAbsorbAtSessionResult(const AtSession::Result& r) {
    // RX: ModemUart вызывает AtSession::onLine до onRxLine (GsmController.Core).
    // Ошибка/таймаут очереди AT — общий сигнал срыва; OK для Expect::Ok совпадает со строкой OK в handleAwaitLine.
    // _awaitGotCreg / _awaitGotIp ставит только handleAwaitLine (+CREG / SAPBR / IPv4).
    if (r.ok) {
        _awaitOk = true;
    }
    if (r.error || r.timedOut) {
        _awaitError = true;
    }
}

void GsmController::update() {
    _stack.uart.pollRx();
    _stack.at.tick(millis());
    const uint32_t now = millis();
    if (_state == GsmState::READY || _stack.tcp.isConnected() || _stack.tcp.isConnecting() ||
        _stack.tcp.isBusBusy()) {
        _stack.tcp.tick(now);
    }

    drainAtResult_();

    switch (_state) {
    case GsmState::IDLE:
        handleIdle();
        break;
    case GsmState::INIT:
        handleInit();
        break;
    case GsmState::REGISTERING:
        handleRegistering();
        break;
    case GsmState::GPRS_SETUP:
        handleGprsSetup();
        break;
    case GsmState::GPRS_ATTACH:
        handleGprsAttach();
        break;
    case GsmState::GPRS_GETIP:
        handleGprsGetIp();
        break;
    case GsmState::READY:
        handleReady();
        break;
    case GsmState::ERROR:
        handleError();
        break;
    }

    gsmMaybeLogStallWatchdog(now);
}

void GsmController::handleIdle() {}
