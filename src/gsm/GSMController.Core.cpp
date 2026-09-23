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
                 if (self->_stack.at.hasResult()) {
                     const AtSession::Result r = self->_stack.at.takeResult();
                     if (!self->_stack.tcp.consumeAtResult(r)) {
                         self->gsmAbsorbAtSessionResult(r);
                     }
                 }
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

void GSMController::sendAt(const char* cmd, const char* tag, AwaitKind kind, uint32_t timeoutMs) {
    _cmdId++;
    ev(3, _cmdId); // sendAt
    if (kind != AwaitKind::NONE) {
        beginAwait(kind, timeoutMs);
    } else {
        resetAwait();
    }
    // Unified AT pipeline: enqueue into AtSession (ModemUart owns actual UART writes).
    // IMPORTANT: AtSession stores non-owning pointers; GSM FSM guarantees one in-flight request,
    // so we store command in a dedicated member buffer.
    snprintf(_atCmdBuf, sizeof(_atCmdBuf), "%s", cmd ? cmd : "");
    const char* effectiveTag = (tag && tag[0]) ? tag : nullptr;
    AtSession::Request r{ _atCmdBuf, timeoutMs, atExpectMask(AtSession::Expect::Ok), nullptr, effectiveTag };
    (void)_stack.at.enqueue(r);
    _lastCommandTime = millis();
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
        case GSMState::GPRS_ACTIVATE:   return "GPRS_ACTIVATE";
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
    if (_state == GSMState::READY && newState != GSMState::READY && _ntpStep != NtpStep::Idle) {
        ntpFail_("left READY");
    }
    _state = newState;
    _stateStartTime = millis();
    _lastCommandTime = 0;
    resetAwait();
    clearResponse();
    ev(2, (uint16_t)newState); // enter state

    if (newState == GSMState::READY) {
        // Successful bring-up: reset reattach backoff.
        _reattachBackoffStep = 0;
        _reattachCooldownUntilMs = 0;
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
        case GSMState::GPRS_ACTIVATE:   return "GPRS_ACTIVATE";
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

