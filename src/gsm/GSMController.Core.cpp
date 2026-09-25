/**
 * @file GSMController.Core.cpp
 * @brief “Ядро” GSMController: RX hooks, update-диспетчер, общие утилиты и состояние.
 *
 * Цель разбиения:
 * - Убрать монолит `src/GSMController.cpp` из корня, привести структуру к `src/gsm/`.
 * - Разделить “диспетчер/утилиты” и FSM-обработчики состояний (см. `GSMController.Fsm.*.cpp`),
 *   чтобы файл было проще сопровождать и чтобы инклюды были явными.
 *
 * Инварианты по памяти:
 * - Никаких `String` операций в горячем пути RX/loop.
 * - Используем кольцевой RX-буфер фиксированного размера (без heap и memmove).
 *
 * Запрещено:
 * - Возвращать/хранить большие строки в `String` внутри GSMController.
 * - Делать логирование UART RX/TX как “сырой дамп” в high-frequency путях.
 */
#include "gsm/GSMController.h"

#include "config/Config.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "common/Logger.h"
#include "common/Utils.h"

#include <cstdlib>
#include <cstring>

GSMController::GSMController()
    : _serial(&Serial),
      _stack(*_serial,
             [](void* ctx) {
                 if (!ctx) return;
                 auto* self = static_cast<GSMController*>(ctx);
                 self->_stack.uart.pollRx();
                 self->_stack.at.tick(millis());
                 self->_stack.tcp.tick(millis());
                 self->drainAtResult_();
                 yield();
             },
             this),
      _state(GSMState::IDLE),
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
        auto* self = static_cast<GSMController*>(ctx);
        self->_stack.at.onLine(line);
        if (self->_state == GSMState::READY || self->_stack.tcp.isConnected() || self->_stack.tcp.isConnecting()) {
            self->_stack.tcp.onLine(line);
        }
        self->onRxLine(line);
    }, this);
    _stack.uart.setByteHandler([](void* ctx, char c) {
        if (!ctx) return;
        auto* self = static_cast<GSMController*>(ctx);
        self->_lastRxByteMs = millis();
        self->appendResponseChar(c);
        self->_stack.at.onByte(c);
        if (self->_state == GSMState::READY || self->_stack.tcp.isConnected() || self->_stack.tcp.isConnecting()) {
            self->_stack.tcp.onByte(c);
        }
    }, this);
}

void GSMController::stop() {
    if (_state == GSMState::IDLE) return;
    logger.log("[GSMController] stop()\n");
    _stack.tcp.stop("gsm_stop");
    _stack.tcp.reset();
    _stack.at.reset();
    _retryCount = 0;
    _signal = 0;
    _operator[0] = '\0';
    gsmResetSessionAfterStop();
    changeState(GSMState::IDLE);
}

void GSMController::flushInput() {
    _stack.uart.flushInput();
}

void GSMController::sendRawLogged(const char* s, const char* tag) {
    if (!s) return;
    (void)tag;
    // GSM FSM should not write directly to Serial; use ModemUart/AtSession pipeline.
    // Keep this for bring-up helpers only.
    _stack.uart.writeRaw(s);
}

void GSMController::logRxSnippet(const char* tag) const {
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
    logger.logSerialOnly("[GSMController] << %s: %s\n", tag ? tag : "", out);
}

bool GSMController::sendAt(const char* cmd, const char* tag, AwaitKind kind, uint32_t timeoutMs) {
    _cmdId++;
    ev(3, _cmdId); // sendAt
    // Unified AT pipeline: enqueue into AtSession (ModemUart owns actual UART writes).
    // IMPORTANT: AtSession stores non-owning pointers; GSM FSM guarantees one in-flight request,
    // so we store command in a dedicated member buffer.
    snprintf(_atCmdBuf, sizeof(_atCmdBuf), "%s", cmd ? cmd : "");
    const char* effectiveTag = (tag && tag[0]) ? tag : nullptr;
    AtSession::Request r{ _atCmdBuf, timeoutMs, atExpectMask(AtSession::Expect::Ok), nullptr, effectiveTag };
    if (!_stack.at.enqueue(r)) {
        // Do not arm await — otherwise FSM stalls until a phantom timeout.
        resetAwait();
        _lastCommandTime = 0;
        logger.log("[GSMController] sendAt enqueue full: %s\n", _atCmdBuf);
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

void GSMController::sendCommand(const char* cmd) {
    // Legacy direct-serial path removed; keep wrapper to preserve call sites during migration.
    sendAt(cmd, nullptr, AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS);
}

static const char* gsmStateToString(GSMState s) {
    switch (s) {
        case GSMState::IDLE:            return "IDLE";
        case GSMState::INIT:            return "INIT";
        case GSMState::REGISTERING:     return "REGISTERING";
        case GSMState::GPRS_SETUP:      return "GPRS_SETUP";
        case GSMState::GPRS_ATTACH:     return "GPRS_ATTACH";
        case GSMState::GPRS_GETIP:      return "GPRS_GETIP";
        case GSMState::READY:           return "READY";
        case GSMState::ERROR:           return "ERROR";
        default:                        return "UNKNOWN";
    }
}

void GSMController::changeState(GSMState newState) {
    if (_state != newState) {
        logger.log("[GSMController] state: %s -> %s\n", getStateString(), gsmStateToString(newState));
    }
    if (_state == GSMState::READY && newState != GSMState::READY && _timeStep != TimeStep::Idle) {
        timeFailStep_("left READY");
    }
    _state = newState;
    _stateStartTime = millis();
    _lastCommandTime = 0;
    resetAwait();
    clearResponse();
    ev(2, (uint16_t)newState); // enter state

    if (newState == GSMState::READY) {
        // Sticky GSM bring-up errors clear on READY. Reattach backoff is cleared only after a
        // stable MQTT session (clearReattachBackoff) — not here — so fail→reattach loops back off.
        _errorRecoveryCycles = 0;
        clearGsmBringupErrors_();
        core.logHeapSnapshot("gsm_ready");
    }
    if (newState == GSMState::INIT) {
        _initPhase = GsmInitPhase::HypSendAt;
        _initCfgStep = 0;
    }
    if (newState == GSMState::GPRS_ATTACH) {
        _attachProbeStep = 0;
    }
    _gsmStallPrevFp = 0xffffffffu;
    _gsmStallFpSinceMs = millis();
}

const char* GSMController::getStateString() const {
    switch (_state) {
        case GSMState::IDLE:            return "IDLE";
        case GSMState::INIT:            return "INIT";
        case GSMState::REGISTERING:     return "REGISTERING";
        case GSMState::GPRS_SETUP:      return "GPRS_SETUP";
        case GSMState::GPRS_ATTACH:     return "GPRS_ATTACH";
        case GSMState::GPRS_GETIP:      return "GPRS_GETIP";
        case GSMState::READY:           return "READY";
        case GSMState::ERROR:           return "ERROR";
        default:                        return "UNKNOWN";
    }
}

void GSMController::clearResponse() {
    _respHead = 0;
    _respCount = 0;
}

void GSMController::appendResponseChar(char c) {
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

bool GSMController::responseContains(const char* needle) const {
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

bool GSMController::gsmCgmiResponseManufacturerOk() const {
    return responseContains(GSM::MODEM_VERIFY_MANUFACTURER_SUBSTR) && responseContains("OK");
}

bool GSMController::gsmParseStoredIprBaud(uint32_t& baudOut) const {
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
    if (end == p || u == 0UL || u > 4000000UL) return false;
    baudOut = (uint32_t)u;
    return true;
}

void GSMController::clearGsmBringupErrors_() {
    const ErrorCode cur = core.getErrorManager().get();
    if (cur == ErrorCode::GSM_NO_RESPONSE || cur == ErrorCode::GSM_REG_FAIL ||
        cur == ErrorCode::GSM_APN_FAIL) {
        core.getErrorManager().clear();
    }
}

ErrorCode GSMController::classifyBearerFail_() const {
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

void GSMController::clearReattachBackoff() {
    _reattachBackoffStep = 0;
    _reattachCooldownUntilMs = 0;
}

void GSMController::leaveReadyForReattach_(const char* why) {
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
    if (_reattachBackoffStep < 6) _reattachBackoffStep++;
    uint32_t delayMs = 5000UL << (_reattachBackoffStep - 1); // 5s…160s
    if (delayMs > 180000UL) delayMs = 180000UL;
    const int16_t rssi = _signal;
    if (_signalBer >= 0 && (rssi < 6 || rssi == 99)) {
        delayMs = (delayMs < 60000UL) ? 60000UL : delayMs;
    }
    _reattachCooldownUntilMs = now + delayMs;
    _reattachRequested = false;
    logger.log("[GSMController] Reattach (%s) backoff=%us rssi=%d\n",
               why ? why : "?", (unsigned)(delayMs / 1000UL), (int)rssi);
    // APN credentials may have changed in config — rewrite Contype/APN before attach.
    if (_apnAppliedValid && !apnMatchesApplied_()) {
        changeState(GSMState::GPRS_SETUP);
    } else {
        changeState(GSMState::GPRS_ATTACH);
    }
}

