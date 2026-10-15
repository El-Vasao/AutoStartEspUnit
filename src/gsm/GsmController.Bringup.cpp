/**
 * @file GsmController.Bringup.cpp
 * @brief Bring-up: `UART_BAUD` + post-boot quiet; без синхронного autobaud в `begin()`; NV UART через `AT+IPR?` в INIT.
 */
#include "gsm/GsmController.h"

#include "gsm/GsmInitPhase.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "common/Logger.h"

namespace {
static void waitCooperative(uint32_t ms) {
    const uint32_t start = millis();
    while (millis() - start < ms) {
        core.feedWatchdog();
        core.cooperate();
        delay(0);
    }
}
} // namespace

void GsmController::gsmResetSessionAfterStop() {
    _verifiedModemContactSinceStop = false;
    _firstAtFallbackStartMs = 0;
    _hypNextAttemptMs = 0;
    _initPhase = GsmInitPhase::None;
    _baudSearchActive = false;
    _baudSearchRound = 0;
    _baudSearchBaudIdx = 0;
    _baudSearchDeadlineMs = 0;
    _baudCooldownUntilMs = 0;
    _didIprNvProbeThisCycle = false;
    _didPreBaudSearchCfun = false;
    _resumeSapbrHadIp = false;
    _postResumeTarget = GsmState::REGISTERING;
    _initCgattBusyRetries = 0;
    clearAppliedApn_();
}

void GsmController::gsmNoteModemSoftReboot(bool cfunAlreadyDone) {
    _verifiedModemContactSinceStop = false;
    _firstAtFallbackStartMs = 0;
    _hypNextAttemptMs = 0;
    _baudSearchActive = false;
    _baudSearchRound = 0;
    _baudSearchBaudIdx = 0;
    _baudSearchDeadlineMs = 0;
    _baudCooldownUntilMs = 0;
    _didIprNvProbeThisCycle = false;
    _didPreBaudSearchCfun = cfunAlreadyDone;
}

void GsmController::gsmOpenUart(uint32_t baud) {
    _stack.uart.begin(baud);
    _baud = baud;
}

void GsmController::begin() {
    waitCooperative(Gsm::BOOT_DELAY_MS);
    const uint32_t now = millis();
    const bool fromErrorRecovery = (_state == GsmState::ERROR);

    if (_bringupStartMs == 0 || _state == GsmState::IDLE) {
        _bringupStartMs = now;
    }
    ev(1);

    if (!fromErrorRecovery) {
        gsmResetSessionAfterStop();
    }

    gsmOpenUart(Gsm::UART_BAUD);
    waitCooperative(Gsm::UART_SETTLE_MS);
    flushInput();

    if (!fromErrorRecovery) {
        _waitFirstUntilMs = now + Gsm::POST_BOOT_QUIET_MS;
    } else {
        _waitFirstUntilMs = 0;
    }

    changeState(GsmState::INIT);
    logger.log(LogModule::GsmController, "begin() UART_BAUD=%lu\n", (unsigned long)Gsm::UART_BAUD);
}
