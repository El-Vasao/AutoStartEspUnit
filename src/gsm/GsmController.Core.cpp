/**
 * @file GsmController.Core.cpp
 * @brief “Ядро” GsmController: RX hooks, update-диспетчер, общие утилиты и состояние.
 *
 * Цель разбиения:
 * - Убрать монолит `src/GsmController.cpp` из корня, привести структуру к `src/gsm/`.
 * - Разделить “диспетчер/утилиты” и FSM-обработчики состояний (см. `GsmController.Fsm.*.cpp`),
 *   чтобы файл было проще сопровождать и чтобы инклюды были явными.
 *
 * Инварианты по памяти:
 * - Никаких `String` операций в горячем пути RX/loop.
 * - Используем кольцевой RX-буфер фиксированного размера (без heap и memmove).
 *
 * Запрещено:
 * - Возвращать/хранить большие строки в `String` внутри GsmController.
 * - Делать логирование UART RX/TX как “сырой дамп” в high-frequency путях.
 */
#include "gsm/GsmController.h"

#include "config/Config.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "common/Logger.h"
#include "common/Utils.h"

#include <cstdlib>
#include <cstring>

GsmController::GsmController()
    : _serial(&Serial),
      _stack(*_serial,
             [](void* ctx) {
                 if (!ctx) return;
                 auto* self = static_cast<GsmController*>(ctx);
                 self->_stack.uart.pollRx();
                 self->_stack.at.tick(millis());
                 self->_stack.tcp.tick(millis());
                 self->drainAtResult_();
                 yield();
             },
             this),
      _state(GsmState::IDLE),
      _baud(0),
      _stateStartTime(0),
      _lastCommandTime(0),
      _signal(0),
      _retryCount(0) {
    _operator[0] = '\0';
    _respHead = 0;
    _respCount = 0;
    _stack.uart.setLineHandler([](void* ctx, const char* line) {
        if (!ctx || !line) return;
        auto* self = static_cast<GsmController*>(ctx);
        self->_stack.at.onLine(line);
        if (self->_state == GsmState::READY || self->_stack.tcp.isConnected() || self->_stack.tcp.isConnecting()) {
            self->_stack.tcp.onLine(line);
        }
        self->onRxLine(line);
    }, this);
    _stack.uart.setByteHandler([](void* ctx, char c) {
        if (!ctx) return;
        auto* self = static_cast<GsmController*>(ctx);
        self->_lastRxByteMs = millis();
        if (!self->_stack.uart.isDataMode()) {
            self->appendResponseChar(c);
        }
        self->_stack.at.onByte(c);
        if (self->_state == GsmState::READY || self->_stack.tcp.isConnected() || self->_stack.tcp.isConnecting()) {
            self->_stack.tcp.onByte(c);
        }
    }, this);
}

void GsmController::stop() {
    if (_state == GsmState::IDLE) return;
    logger.log(LogModule::GsmController, "stop()\n");
    voiceReset_();
    _voiceResultReady = false;
    _voicePendingProgramValid = false;
    voiceAbortInboundClipWait_();
    _stack.tcp.stop("gsm_stop");
    _stack.tcp.reset();
    _stack.at.reset();
    _retryCount = 0;
    _signal = 0;
    _operator[0] = '\0';
    gsmResetSessionAfterStop();
    changeState(GsmState::IDLE);
}

void GsmController::flushInput() {
    _stack.uart.flushInput();
}

void GsmController::logRxSnippet(const char* tag) const {
    if (_respCount == 0) return;
    // Логируем короткий “хвост” буфера, чтобы видеть, что модем отвечает,
    // но не спамить большие дампы и не дёргать heap.
    constexpr uint16_t kMax = 96;
    const uint16_t n = (_respCount > kMax) ? kMax : _respCount;
    char out[kMax + 1];
    uint16_t j = 0;
    // Берём последние n байт из кольца.
    for (uint16_t i = 0; i < n && j < kMax; i++) {
        const uint16_t idx = (uint16_t)((_respHead + _respCount - n + i) % RESPONSE_BUF_SIZE);
        char c = _responseBuffer[idx];
        if (c == '\r') continue;
        if (c == '\n') c = ' ';
        const uint8_t u = static_cast<uint8_t>(c);
        // RX буфер может содержать бинарь (например, +IPD payload). Не печатаем его “как есть”.
        if (u < 0x20 || u > 0x7E) {
            out[j++] = '.';
        } else {
            out[j++] = c;
        }
    }
    out[j] = '\0';
    // RX snippets are high-frequency during reconnect storms; keep them off SSE to reduce queue pressure.
    logger.logSerialOnly(LogModule::GsmController, "<< %s: %s\n", tag ? tag : "", out);
}

bool GsmController::sendAt(const char* cmd, const char* tag, AwaitKind kind, uint32_t timeoutMs) {
    _cmdId++;
    ev(3, _cmdId); // sendAt
    // Unified AT pipeline: enqueue into AtSession (ModemUart owns actual UART writes).
    snprintf(_atCmdBuf, sizeof(_atCmdBuf), "%s", cmd ? cmd : "");
    const char* effectiveTag = (tag && tag[0]) ? tag : nullptr;
    AtSession::Request r{ _atCmdBuf, timeoutMs, atExpectMask(AtSession::Expect::Ok), nullptr, effectiveTag };
    if (!_stack.at.enqueue(r)) {
        // Do not arm await — otherwise FSM stalls until a phantom timeout.
        resetAwait();
        _lastCommandTime = 0;
        logger.log(LogModule::GsmController, "sendAt enqueue full: %s\n", _atCmdBuf);
        return false;
    }
    if (kind != AwaitKind::NONE) {
        beginAwait(kind, timeoutMs);
    } else {
        resetAwait();
    }
    _lastCommandTime = millis();
    return true;
}

static const char* gsmStateToString(GsmState s) {
    switch (s) {
        case GsmState::IDLE:            return "IDLE";
        case GsmState::INIT:            return "INIT";
        case GsmState::REGISTERING:     return "REGISTERING";
        case GsmState::GPRS_SETUP:      return "GPRS_SETUP";
        case GsmState::GPRS_ATTACH:     return "GPRS_ATTACH";
        case GsmState::GPRS_GETIP:      return "GPRS_GETIP";
        case GsmState::READY:           return "READY";
        case GsmState::ERROR:           return "ERROR";
        default:                        return "UNKNOWN";
    }
}

void GsmController::changeState(GsmState newState) {
    if (_state != newState) {
        logger.log(LogModule::GsmController, "state: %s -> %s\n", getStateString(), gsmStateToString(newState));
    }
    if (_state == GsmState::READY && newState != GsmState::READY && _timeStep != TimeStep::Idle) {
        timeFailStep_("left READY");
    }
    _state = newState;
    _stateStartTime = millis();
    _lastCommandTime = 0;
    resetAwait();
    clearResponse();
    ev(2, (uint16_t)newState); // enter state

    if (newState == GsmState::READY) {
        // Sticky GSM bring-up errors clear on READY. Reattach backoff is cleared only after a
        // stable MQTT session (clearReattachBackoff) — not here — so fail→reattach loops back off.
        _errorRecoveryCycles = 0;
        clearGsmBringupErrors_();
        core.logHeapSnapshot("gsm_ready");
    }
    if (newState == GsmState::INIT) {
        _initPhase = GsmInitPhase::HypSendAt;
    }
    if (newState == GsmState::GPRS_ATTACH) {
        _attachProbeStep = 0;
    }
    _gsmStallPrevFp = 0xffffffffu;
    _gsmStallFpSinceMs = millis();
}

const char* GsmController::getStateString() const {
    switch (_state) {
        case GsmState::IDLE:            return "IDLE";
        case GsmState::INIT:            return "INIT";
        case GsmState::REGISTERING:     return "REGISTERING";
        case GsmState::GPRS_SETUP:      return "GPRS_SETUP";
        case GsmState::GPRS_ATTACH:     return "GPRS_ATTACH";
        case GsmState::GPRS_GETIP:      return "GPRS_GETIP";
        case GsmState::READY:           return "READY";
        case GsmState::ERROR:           return "ERROR";
        default:                        return "UNKNOWN";
    }
}

void GsmController::clearResponse() {
    _respHead = 0;
    _respCount = 0;
}

void GsmController::appendResponseChar(char c) {
    // Кольцевой буфер: O(1), без memmove.
    if (_respCount < RESPONSE_BUF_SIZE) {
        const uint16_t pos = (uint16_t)((_respHead + _respCount) % RESPONSE_BUF_SIZE);
        _responseBuffer[pos] = c;
        _respCount++;
    } else {
        // Переполнение: вытесняем самый старый байт.
        _responseBuffer[_respHead] = c;
        _respHead = (uint16_t)((_respHead + 1) % RESPONSE_BUF_SIZE);
    }
}

bool GsmController::responseContains(const char* needle) const {
    if (!needle || needle[0] == '\0') return false;
    const size_t m = strlen(needle);
    if (m == 0) return false;
    if (_respCount < m) return false;

    // Наивный поиск по кольцу (N<=256, m обычно 2..8) — быстрее и проще, чем memmove+strstr.
    for (uint16_t i = 0; i + m <= _respCount; i++) {
        bool ok = true;
        for (size_t j = 0; j < m; j++) {
            const uint16_t idx = (uint16_t)((_respHead + i + (uint16_t)j) % RESPONSE_BUF_SIZE);
            if (_responseBuffer[idx] != needle[j]) {
                ok = false;
                break;
            }
        }
        if (ok) return true;
    }
    return false;
}

bool GsmController::gsmCgmiResponseManufacturerOk() const {
    return responseContains(Gsm::MODEM_VERIFY_MANUFACTURER_SUBSTR) && responseContains("OK");
}

bool GsmController::gsmParseStoredIprBaud(uint32_t& baudOut) const {
    if (!responseContains("+IPR:")) return false;
    const uint16_t capRaw = (_respCount > 64) ? 64 : _respCount;
    char scratch[80];
    const uint16_t cap = (capRaw < sizeof(scratch) - 1) ? capRaw : (uint16_t)(sizeof(scratch) - 1);
    for (uint16_t i = 0; i < cap; i++) {
        scratch[i] =
            _responseBuffer[(uint16_t)((_respHead + _respCount - cap + i) % RESPONSE_BUF_SIZE)];
    }
    scratch[cap] = '\0';
    const char* p = strstr(scratch, "+IPR:");
    if (!p) return false;
    p += 5;
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    char* end = nullptr;
    const unsigned long u = strtoul(p, &end, 10);
    if (end == p || u == 0UL || u > Gsm::UART_BAUD_PARSE_MAX) return false;
    baudOut = (uint32_t)u;
    return true;
}

void GsmController::clearGsmBringupErrors_() {
    auto& err = core.getErrorManager();
    err.clear(ErrorCode::GSM_NO_RESPONSE);
    err.clear(ErrorCode::GSM_REG_FAIL);
    err.clear(ErrorCode::GSM_APN_FAIL);
}

ErrorCode GsmController::classifyBearerFail_() const {
    // Antenna loss / out of coverage: deregistered or no usable RF — not an APN config problem.
    if (_cregStat != 1 && _cregStat != 5) {
        return ErrorCode::GSM_REG_FAIL;
    }
    // _signalBer < 0 ⇒ never got CSQ; do not treat constructor _signal=0 as dead RF.
    if (_signalBer >= 0 && (_signal == 99 || _signal <= 1)) {
        return ErrorCode::GSM_REG_FAIL;
    }
    return ErrorCode::GSM_APN_FAIL;
}

void GsmController::clearReattachBackoff() {
    _reattachBackoffStep = 0;
    _reattachCooldownUntilMs = 0;
}

void GsmController::leaveReadyForReattach_(const char* why) {
    // Stop TCP first so CIPCLOSE finishes before SAPBR on the shared AT bus.
    if (_stack.tcp.isConnected() || _stack.tcp.isConnecting() || _stack.tcp.isBusBusy()) {
        _stack.tcp.stop(why ? why : "reattach");
    }
    if (_stack.tcp.isBusBusy() || _stack.at.isBusy() || _stack.at.hasResult()) {
        // Keep request pending; handleReady retries when bus is idle.
        _reattachRequested = true;
        return;
    }
    const uint32_t now = millis();
    if (_reattachBackoffStep < Gsm::REATTACH_BACKOFF_MAX_STEP) _reattachBackoffStep++;
    uint32_t delayMs = Gsm::REATTACH_BACKOFF_BASE_MS << (_reattachBackoffStep - 1);
    if (delayMs > Gsm::REATTACH_BACKOFF_MAX_MS) delayMs = Gsm::REATTACH_BACKOFF_MAX_MS;
    const int16_t rssi = _signal;
    if (_signalBer >= 0 && (rssi < Gsm::REATTACH_WEAK_RSSI_MAX || rssi == Gsm::REATTACH_RSSI_UNKNOWN)) {
        delayMs = (delayMs < Gsm::REATTACH_WEAK_RSSI_FLOOR_MS) ? Gsm::REATTACH_WEAK_RSSI_FLOOR_MS : delayMs;
    }
    _reattachCooldownUntilMs = now + delayMs;
    _reattachRequested = false;
    logger.log(LogModule::GsmController, "Reattach (%s) backoff=%us rssi=%d\n",
               why ? why : "?", (unsigned)(delayMs / 1000UL), (int)rssi);
    // APN credentials may have changed in config — rewrite Contype/APN before attach.
    if (_apnAppliedValid && !apnMatchesApplied_()) {
        changeState(GsmState::GPRS_SETUP);
    } else {
        changeState(GsmState::GPRS_ATTACH);
    }
}

