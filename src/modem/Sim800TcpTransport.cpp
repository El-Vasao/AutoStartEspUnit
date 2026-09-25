#include "modem/Sim800TcpTransport.h"
#include <stdio.h>
#include <string.h>
#include "common/Constants.h"
#include "common/Logger.h"

namespace {
#if defined(SERIAL_DEBUG)
constexpr bool kTcpWantVerbose = true;
#else
constexpr bool kTcpWantVerbose = Sim800Tcp::TCP_VERBOSE_LOG;
#endif
} // namespace

// Narrative TCP lines always log; +IPD/CIPSTART-chatter only when SERIAL_DEBUG or Sim800Tcp::TCP_VERBOSE_LOG.

static bool isControlLine_(const char* s, size_t len) {
    if (!s || len == 0) return true;
    // Trim trailing CR/LF/spaces
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' || s[len - 1] == ' ' || s[len - 1] == '\t')) {
        len--;
    }
    if (len == 0) return true;

    // Fast-path: common modem control lines / AT echo
    if (len >= 2 && s[0] == 'A' && s[1] == 'T') return true;       // "AT...", "ATE1", ...
    if (len == 2 && s[0] == 'O' && s[1] == 'K') return true;        // "OK"
    if (len >= 5 && memcmp(s, "ERROR", 5) == 0) return true;        // "ERROR"
    if (len >= 4 && memcmp(s, "RDY", 3) == 0) return true;          // "RDY"
    if (len >= 7 && memcmp(s, "CLOSED", 6) == 0) return true;       // "CLOSED"
    if (len >= 10 && memcmp(s, "CONNECT OK", 10) == 0) return true; // "CONNECT OK"
    if (len >= 12 && memcmp(s, "CONNECT FAIL", 12) == 0) return true;
    if (len >= 15 && memcmp(s, "ALREADY CONNECT", 15) == 0) return true;
    if (len >= 8 && memcmp(s, "CLOSE OK", 8) == 0) return true;
    if (len >= 7 && memcmp(s, "SHUT OK", 7) == 0) return true;
    if (len >= 7 && memcmp(s, "SEND OK", 7) == 0) return true;      // "SEND OK"
    if (len >= 9 && memcmp(s, "SEND FAIL", 9) == 0) return true;    // "SEND FAIL"
    if (len >= 1 && s[0] == '>') return true;                       // prompt line (">")

    // URCs typically start with '+'; also stripped forms if +IPD matcher ate '+'
    if (s[0] == '+') return true;
    if (len >= 4 && memcmp(s, "CSQ:", 4) == 0) return true;
    if (len >= 5 && memcmp(s, "CREG:", 5) == 0) return true;
    if (len >= 5 && memcmp(s, "COPS:", 5) == 0) return true;
    if (len >= 4 && memcmp(s, "IPD,", 4) == 0) return true;

    return false;
}

void Sim800TcpTransport::clearIpConfigFlags_() {
    _ipConfigDone = false;
    _ipConfigEnqueued = false;
    _ipConfigOkMask = 0;
}

void Sim800TcpTransport::reset() {
    _connected = false;
    _connecting = false;
    _sendInProgress = false;
    _modemTxLocked = false;
    clearIpConfigFlags_();
    _didInitialCipShut = false;
    _closeQueued = false;
    _recoverQueued = false;
    _needsBearerReattach = false;
    _stackRecoverCount = 0;
    _connectStartMs = 0;
    _sendWatchMs = 0;
    _lastStackRecoverMs = 0;
    _lastConnectAttemptMs = 0;
    _flushRequested = false;
    _rawLineLen = 0;
    _promptLeak = 0;
    _port = 0;
    _host[0] = '\0';
    _cmdStart[0] = '\0';
    _cmdSend[0] = '\0';
    _rxHead = 0;
    _rxCount = 0;
    _rxOverflow = false;
    _txLen = 0;
    _sendLen = 0;
    _ipState = IpState::Idle;
    _ipLen = 0;
    _ipRead = 0;
}

void Sim800TcpTransport::noteConnectOk_() {
    _stackRecoverCount = 0;
    _needsBearerReattach = false;
}

void Sim800TcpTransport::clearTx_() {
    _sendInProgress = false;
    _sendWatchMs = 0;
    _txLen = 0;
    _sendLen = 0;
    _flushRequested = false;
}

void Sim800TcpTransport::clearRx_() {
    _rxHead = 0;
    _rxCount = 0;
    _rxOverflow = false;
    _rawLineLen = 0;
    _promptLeak = 0;
    if (_ipState == IpState::ReadData) {
        _at.uart().setDataMode(false);
    }
    _ipState = IpState::Idle;
    _ipLen = 0;
    _ipRead = 0;
}

bool Sim800TcpTransport::takeRxOverflow() {
    if (!_rxOverflow) return false;
    _rxOverflow = false;
    return true;
}

void Sim800TcpTransport::fillRxForensic(RxForensic& out) const {
    out.ipState = static_cast<uint8_t>(_ipState);
    out.ipLen = _ipLen;
    out.ipRead = _ipRead;
    out.rxCount = _rxCount;
    out.sendInProgress = _sendInProgress;
    out.modemTxLocked = _modemTxLocked;
    out.connected = _connected;
    out.ipConfigDone = _ipConfigDone;
}

void Sim800TcpTransport::logRxForensic(const char* why) const {
    logger.log(
        "[Sim800Tcp] RX forensic (%s): ipState=%u ipLen=%u ipRead=%u rxRing=%u "
        "send=%u txLock=%u conn=%u ciphead=%u\n",
        why ? why : "?", (unsigned)_ipState, (unsigned)_ipLen, (unsigned)_ipRead,
        (unsigned)_rxCount, (unsigned)_sendInProgress, (unsigned)_modemTxLocked,
        (unsigned)_connected, (unsigned)_ipConfigDone);
}

bool Sim800TcpTransport::discardingTcpPayload_(bool fromIpd) const {
    // Requires CIPHEAD=1 (see startConnect_): framed +IPD is real TCP and must arrive mid-CIPSEND.
    // Raw (non-+IPD) bytes during send-epoch are modem echo/URC — keep them out of MQTT RX.
    if (fromIpd) return false;
    return _sendInProgress || _modemTxLocked;
}

void Sim800TcpTransport::endSendEpoch_() {
    clearTx_();
    _modemTxLocked = false;
}

void Sim800TcpTransport::forceStackRecover_(const char* reason) {
    const uint32_t now = _lastNowMs ? _lastNowMs : millis();
    if (_lastStackRecoverMs != 0 &&
        (int32_t)(now - _lastStackRecoverMs) < (int32_t)Sim800Tcp::STACK_RECOVER_COOLDOWN_MS) {
        return;
    }
    _lastStackRecoverMs = now;
    clearIpConfigFlags_();
    _didInitialCipShut = false;
    _closeQueued = false;
    if (!_recoverQueued) {
        if (_at.enqueueHigh({ "AT+CIPSHUT", 10000, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPSHUT" })) {
            _recoverQueued = true;
            logger.log("[Sim800Tcp] stack recover CIPSHUT (%s)\n", reason ? reason : "?");
        } else {
            logger.log("[Sim800Tcp] stack recover CIPSHUT enqueue failed (%s)\n", reason ? reason : "?");
        }
    }
    if (_stackRecoverCount < 255) _stackRecoverCount++;
    if (_stackRecoverCount >= Sim800Tcp::STACK_RECOVER_REATTACH_THRESHOLD) {
        _needsBearerReattach = true;
        logger.log("[Sim800Tcp] stack recover threshold -> bearer reattach\n");
    }
}

void Sim800TcpTransport::abandonConnect(const char* reason) {
    if (!_connecting && !_connected && !_sendInProgress && !_modemTxLocked) return;
    logger.log("[Sim800Tcp] abandonConnect: %s\n", reason ? reason : "?");
    _connecting = false;
    _connected = false;
    endSendEpoch_();
    forceStackRecover_(reason ? reason : "abandon");
}

bool Sim800TcpTransport::connectStart(const char* host, uint16_t port) {
    if (!host || !host[0]) return false;
    if (_connecting || _connected) return false;
    // Do not stack CIPSTART on top of CIPCLOSE/CIPSHUT, in-flight AT, or send-epoch.
    if (_closeQueued || _recoverQueued) return false;
    if (_modemTxLocked || _sendInProgress || _txLen != 0) return false;
    if (_at.isBusy() || _at.hasResult()) return false;

    const uint32_t now = millis();
    if (_lastConnectAttemptMs != 0 &&
        (int32_t)(now - _lastConnectAttemptMs) < (int32_t)Sim800Tcp::CONNECT_RETRY_COOLDOWN_MS) {
        return false;
    }

    strncpy(_host, host, sizeof(_host) - 1);
    _host[sizeof(_host) - 1] = '\0';
    _port = port;
    _connecting = true;
    _connectStartMs = now;
    _lastConnectAttemptMs = now;
    clearTx_(); // never leak a previous CIPSEND payload onto a new TCP session
    logger.logSerialOnly("[Sim800Tcp] connectStart host=%s port=%u\n", _host, (unsigned)_port);
    if (!startConnect_()) {
        _connecting = false;
        _connectStartMs = 0;
        logger.log("[Sim800Tcp] connectStart enqueue failed\n");
        return false;
    }
    return true;
}

void Sim800TcpTransport::stop(const char* reason) {
    const bool wasConnectedOrConnecting = (_connected || _connecting);
    _connected = false;
    _connecting = false;
    clearTx_();
    _promptLeak = 0;
    // Cooldown so the next MQTT tick does not CIPSTART-storm.
    _lastConnectAttemptMs = nowMs_();
    if (reason && reason[0]) {
        logger.log("[Sim800Tcp] stop(): %s\n", reason);
    } else {
        logger.logSerialOnly("[Sim800Tcp] stop()\n");
    }
    // Only CIPCLOSE if we still believe a socket exists (CLOSED URC already tore it down).
    if (wasConnectedOrConnecting && !_closeQueued) {
        _closeQueued = true;
        // CIPMUX=0 (single connection): CIPCLOSE without link id.
        AtSession::Request r{ "AT+CIPCLOSE", 3000, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPCLOSE" };
        (void)_at.enqueue(r);
    }
}

bool Sim800TcpTransport::enqueueIpConfig_() {
    // Order: CIPRXGET=0 → CIPHEAD=1 → CIPMUX=0. CIPHEAD is required for discardingTcpPayload_.
    if (!_at.enqueue({ "AT+CIPRXGET=0", 3000, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPRXGET0" })) {
        return false;
    }
    if (!_at.enqueue({ "AT+CIPHEAD=1", 3000, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPHEAD1" })) {
        return false;
    }
    if (!_at.enqueue({ "AT+CIPMUX=0", 3000, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPMUX0" })) {
        return false;
    }
    _ipConfigEnqueued = true;
    _ipConfigOkMask = 0;
    _ipConfigDone = false;
    logger.log("[Sim800Tcp] IP config enqueue (await CIPHEAD OK)\n");
    return true;
}

bool Sim800TcpTransport::enqueueCipStart_() {
    // CIPMUX=0: CIPSTART without link id.
    snprintf(_cmdStart, sizeof(_cmdStart), "AT+CIPSTART=\"TCP\",\"%s\",%u", _host, (unsigned)_port);
    if (!_at.enqueue({ _cmdStart, Sim800Tcp::CIPSTART_ACCEPT_TIMEOUT_MS, atExpectMask(AtSession::Expect::Ok),
                       nullptr, "CIPSTART" })) {
        return false;
    }
    return true;
}

bool Sim800TcpTransport::startConnect_() {
    // Warm modem after ESP-only reboot may still hold a stale TCP session — clear once.
    if (!_didInitialCipShut) {
        if (!_at.enqueue({ "AT+CIPSHUT", 10000, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPSHUT" })) {
            return false;
        }
        _didInitialCipShut = true;
        clearIpConfigFlags_();
        logger.log("[Sim800Tcp] CIPSHUT (initial, warm-safe)\n");
        // CIPSTART after CIPSHUT OK → consumeAtResult continues config.
        return true;
    }

    if (!_ipConfigDone) {
        if (!_ipConfigEnqueued) {
            if (!enqueueIpConfig_()) return false;
        }
        // Wait for all three OK bits before CIPSTART (see consumeAtResult).
        return true;
    }

    return enqueueCipStart_();
}

void Sim800TcpTransport::tick(uint32_t nowMs) {
    _lastNowMs = nowMs;
    _at.tick(nowMs);

    // Connect / send watchdogs (modem hang / stuck flags).
    if (_connecting && _connectStartMs != 0 &&
        (nowMs - _connectStartMs) >= Sim800Tcp::CONNECT_WATCHDOG_MS) {
        logger.log("[Sim800Tcp] connect watchdog\n");
        _connecting = false;
        _connected = false;
        _connectStartMs = 0;
        endSendEpoch_();
        forceStackRecover_("connect_wd");
    }
    if (_sendInProgress && _sendWatchMs != 0 &&
        (nowMs - _sendWatchMs) >= Sim800Tcp::SEND_WATCHDOG_MS) {
        logger.log("[Sim800Tcp] send watchdog\n");
        endSendEpoch_();
        _connected = false;
        _connecting = false;
        forceStackRecover_("send_wd");
    }

    // SoftAP/write pump: drain TCP-owned AtSession results while we own the bus.
    if (_at.hasResult() &&
        (_connecting || _sendInProgress || _modemTxLocked || _closeQueued || _recoverQueued)) {
        (void)consumeAtResult(_at.takeResult());
    }

    // Start sealed CIPSEND when flushSend() was called.
    if (_flushRequested && _connected && !_sendInProgress && !_modemTxLocked && _txLen > 0) {
        startSend_();
    }
}

bool Sim800TcpTransport::consumeAtResult(const AtSession::Result& r) {
    if (!r.tag) return false;
    if (strcmp(r.tag, "CIPCLOSE") == 0) {
        _closeQueued = false;
        return true;
    }
    if (strcmp(r.tag, "CIPSHUT") == 0) {
        _recoverQueued = false;
        _modemTxLocked = false; // recover completed — release send-epoch lock
        clearIpConfigFlags_();
        // After warm CIPSHUT (or recover), re-apply CIPHEAD before CIPSTART.
        if (_connecting && !_ipConfigDone) {
            if (!enqueueIpConfig_()) {
                _connecting = false;
                logger.log("[Sim800Tcp] IP config enqueue failed after CIPSHUT\n");
            }
        }
        return true;
    }
    if (strcmp(r.tag, "CIPSTART") == 0) {
        // Command acceptance; actual connect is signaled via URC CONNECT OK / CONNECT FAIL.
        if (r.timedOut || r.error) {
            logger.log("[Sim800Tcp] CIPSTART accept failed (timeout=%u error=%u)\n",
                       (unsigned)r.timedOut, (unsigned)r.error);
            _connecting = false;
            _connected = false;
            _connectStartMs = 0;
            _lastConnectAttemptMs = _lastNowMs ? _lastNowMs : millis();
            forceStackRecover_("cipstart_accept");
        } else if (r.ok) {
            if (kTcpWantVerbose) {
                logger.log("[Sim800Tcp] CIPSTART accepted (waiting URC)\n");
            }
        }
        return true;
    }
    if (strcmp(r.tag, "CIPSEND") == 0) {
        if (r.gotPrompt) {
            // CIPSEND=<len>: send exactly the length frozen at startSend_ (no Ctrl+Z in len mode).
            const uint16_t n = (_sendLen <= _txLen) ? _sendLen : _txLen;
            if (n) _at.uart().writeBytes(_tx, n);
            _sendWatchMs = _lastNowMs ? _lastNowMs : millis(); // wait for SEND OK / SEND FAIL
            // Keep _modemTxLocked + _sendInProgress until SEND OK (AtSession is Idle after '>').
        } else if (r.timedOut || r.error) {
            logger.log("[Sim800Tcp] CIPSEND accept failed (timeout=%u error=%u)\n",
                       (unsigned)r.timedOut, (unsigned)r.error);
            endSendEpoch_();
            _connected = false;
            _connecting = false;
            forceStackRecover_("cipsend_accept");
        }
        return true;
    }
    if (strcmp(r.tag, "CIPRXGET0") == 0 || strcmp(r.tag, "CIPHEAD1") == 0 ||
        strcmp(r.tag, "CIPMUX0") == 0) {
        uint8_t bit = 0;
        if (strcmp(r.tag, "CIPRXGET0") == 0) bit = 0x01;
        else if (strcmp(r.tag, "CIPHEAD1") == 0) bit = 0x02;
        else bit = 0x04;

        if (r.timedOut || r.error || !r.ok) {
            logger.log("[Sim800Tcp] IP config fail tag=%s timeout=%u error=%u ok=%u\n", r.tag,
                       (unsigned)r.timedOut, (unsigned)r.error, (unsigned)r.ok);
            clearIpConfigFlags_();
            _connecting = false;
            _connected = false;
            _connectStartMs = 0;
            forceStackRecover_("ip_config");
            return true;
        }
        _ipConfigOkMask = (uint8_t)(_ipConfigOkMask | bit);
        if (_ipConfigOkMask == 0x07) {
            _ipConfigDone = true;
            _ipConfigEnqueued = false;
            logger.log("[Sim800Tcp] IP config OK (CIPHEAD=1 confirmed)\n");
            if (_connecting) {
                if (!enqueueCipStart_()) {
                    logger.log("[Sim800Tcp] CIPSTART enqueue failed after IP config\n");
                    _connecting = false;
                    forceStackRecover_("cipstart_after_cfg");
                }
            }
        }
        return true;
    }
    if (strcmp(r.tag, "CIPMODE") == 0 || strcmp(r.tag, "CIPQSEND") == 0) {
        return true; // drain leftover TCP stack config replies
    }
    return false;
}

void Sim800TcpTransport::onLine(const char* line) {
    if (!line || !line[0]) return;
    // Connection URCs — do not treat bare ERROR (shared UART with GSM AT).
    if (strstr(line, "CONNECT OK") != nullptr) {
        _lastConnectOkMs = _lastNowMs;
        logger.log("[Sim800Tcp] TCP connected\n");
        _connected = true;
        _connecting = false;
        _connectStartMs = 0;
        endSendEpoch_(); // new socket — drop any pre-connect staging
        noteConnectOk_();
        return;
    }
    if (strstr(line, "ALREADY CONNECT") != nullptr) {
        logger.log("[Sim800Tcp] TCP already connected\n");
        _connected = true;
        _connecting = false;
        _connectStartMs = 0;
        endSendEpoch_();
        noteConnectOk_();
        return;
    }
    if (strcmp(line, "CLOSED") == 0) {
        const uint32_t dt = (_lastConnectOkMs != 0) ? (_lastNowMs - _lastConnectOkMs) : 0;
        logger.log("[Sim800Tcp] TCP closed (uptime=%ums)\n", (unsigned)dt);
        const bool wasSending = _modemTxLocked || _sendInProgress;
        _connected = false;
        _connecting = false;
        clearTx_();
        // Keep bus locked if mid-send so CIPSTART cannot race until CIPSHUT finishes.
        if (wasSending) {
            _modemTxLocked = true;
            forceStackRecover_("closed_mid_send");
        }
        _lastConnectAttemptMs = _lastNowMs ? _lastNowMs : millis();
        return;
    }
    if (strstr(line, "CONNECT FAIL") != nullptr) {
        static uint32_t s_lastFailLogMs = 0;
        const uint32_t now = nowMs_();
        if (s_lastFailLogMs == 0 ||
            (int32_t)(now - s_lastFailLogMs) >= (int32_t)Sim800Tcp::CONNECT_RETRY_COOLDOWN_MS) {
            s_lastFailLogMs = now;
            logger.log("[Sim800Tcp] TCP connect failed\n");
        } else {
            logger.logSerialOnly("[Sim800Tcp] TCP connect failed\n");
        }
        _connected = false;
        _connecting = false;
        _connectStartMs = 0;
        endSendEpoch_();
        _lastConnectAttemptMs = now;
        return;
    }
    if (strcmp(line, "SEND OK") == 0) {
        endSendEpoch_();
        return;
    }
    if (strstr(line, "SEND FAIL") != nullptr) {
        logger.log("[Sim800Tcp] SEND FAIL — stack recover\n");
        endSendEpoch_();
        _connected = false;
        _connecting = false;
        forceStackRecover_("send_fail");
        return;
    }
}

void Sim800TcpTransport::abortIpdMatch_(IpState matchedUntil, char c) {
    // Replay bytes already consumed by the +IPD matcher so URCs like +CSQ keep their '+'.
    const char* prefix = "";
    switch (matchedUntil) {
    case IpState::MatchI: prefix = "+"; break;
    case IpState::MatchP: prefix = "+I"; break;
    case IpState::MatchD: prefix = "+IP"; break;
    case IpState::MatchComma: prefix = "+IPD"; break;
    case IpState::ReadLen: prefix = "+IPD,"; break;
    default: break;
    }
    _ipState = IpState::Idle;
    for (const char* p = prefix; *p; ++p) {
        feedRawByte_(*p);
    }
    if (matchedUntil == IpState::ReadLen && _ipLen > 0) {
        char digits[6];
        snprintf(digits, sizeof(digits), "%u", (unsigned)_ipLen);
        for (char* p = digits; *p; ++p) {
            feedRawByte_(*p);
        }
        _ipLen = 0;
        _ipRead = 0;
    }
    feedRawByte_(c);
}

void Sim800TcpTransport::feedRawByte_(char c) {
    // Raw path (fallback / modem text): CRLF control-line filter into MQTT RX.
    // Do not treat 0x20 as text start — MQTT CONNACK begins with that byte.
    const uint8_t ub = (uint8_t)c;
    const bool printable = (ub == '\r' || ub == '\n' || ub == '\t' || (ub >= 0x20 && ub <= 0x7E));

    // Strip leaked CIPSEND '>' before printable/binary split.
    if (_connected && _promptLeak != 0) {
        if (_promptLeak == 1) {
            if (ub == '\r') {
                _promptLeak = 2;
                return;
            }
            if (ub == '\n') {
                _promptLeak = 0;
                return;
            }
            _promptLeak = 0;
            // Fall through: first real TCP byte after a lone '>'.
        } else if (_promptLeak == 2) {
            if (ub == '\n') {
                _promptLeak = 0;
                return;
            }
            _promptLeak = 0;
            // Fall through
        }
    }

    if (!printable) {
        // Binary byte: flush any pending line bytes as payload and pass through.
        _promptLeak = 0;
        for (uint8_t i = 0; i < _rawLineLen; i++) pushRx_((uint8_t)_rawLineBuf[i]);
        _rawLineLen = 0;
        if (_connected) pushRx_(ub);
        return;
    }

    // New prompt leak: '>' must never be line-buffered as modem text (would glue to MQTT bytes).
    if (_connected && _rawLineLen == 0 && ub == '>') {
        _promptLeak = 1;
        return;
    }

    // Line start while connected: only buffer plausible modem-text starts; else TCP payload.
    if (_connected && _rawLineLen == 0) {
        if (ub == '\r' || ub == '\n') {
            return;
        }
        const bool maybeModemTextStart =
            (ub == '+') || // URCs like +CREG, +CSQ, +IPD...
            (ub == 'A') || // AT echoes / ALREADY CONNECT
            (ub == 'O') || // OK
            (ub == 'E') || // ERROR
            (ub == 'C') || // CLOSED / CONNECT / CLOSE OK / CSQ (stripped)
            (ub == 'S') || // SEND OK/FAIL / SHUT OK
            (ub == 'R') || // RDY
            (ub == 'N') || // NO CARRIER (rare)
            (ub == 'F') || // FAIL fragments
            (ub == 'I');   // IPD, (stripped +)

        if (!maybeModemTextStart) {
            pushRx_(ub);
            return;
        }
    }

    if (_rawLineLen + 1 >= RAW_LINE_BUF_SIZE) {
        // Too long for a control line: treat as payload.
        for (uint8_t i = 0; i < _rawLineLen; i++) pushRx_((uint8_t)_rawLineBuf[i]);
        _rawLineLen = 0;
        if (_connected) pushRx_(ub);
        return;
    }

    _rawLineBuf[_rawLineLen++] = c;
    if (c != '\n') {
        return;
    }

    // Got a CRLF-terminated line candidate.
    if (!isControlLine_(_rawLineBuf, _rawLineLen) && _connected) {
        for (uint8_t i = 0; i < _rawLineLen; i++) pushRx_((uint8_t)_rawLineBuf[i]);
    }
    _rawLineLen = 0;
}

void Sim800TcpTransport::onByte(char c) {
    // +IPD state machine (CIPHEAD=1). While matching/reading, framing owns the byte.
    switch (_ipState) {
        case IpState::Idle:
            if (c == '+') {
                _ipState = IpState::MatchI;
                return;
            }
            break;
        case IpState::MatchI:
            if (c == 'I') {
                _ipState = IpState::MatchP;
                return;
            }
            abortIpdMatch_(IpState::MatchI, c);
            return;
        case IpState::MatchP:
            if (c == 'P') {
                _ipState = IpState::MatchD;
                return;
            }
            abortIpdMatch_(IpState::MatchP, c);
            return;
        case IpState::MatchD:
            if (c == 'D') {
                _ipState = IpState::MatchComma;
                return;
            }
            abortIpdMatch_(IpState::MatchD, c);
            return;
        case IpState::MatchComma:
            if (c == ',') {
                _ipLen = 0;
                _ipRead = 0;
                _ipState = IpState::ReadLen;
                return;
            }
            abortIpdMatch_(IpState::MatchComma, c);
            return;
        case IpState::ReadLen:
            // Some firmwares may insert spaces/CRLF around length, be tolerant.
            if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
                return;
            }
            if (c >= '0' && c <= '9') {
                const uint32_t v = (uint32_t)_ipLen * 10UL + (uint32_t)(c - '0');
                _ipLen = (v > 65535UL) ? 65535 : (uint16_t)v;
                return;
            }
            if (c == ':') {
                if (kTcpWantVerbose) {
                    logger.log("[Sim800Tcp] +IPD len=%u\n", (unsigned)_ipLen);
                }
                _ipRead = 0;
                _at.uart().setDataMode(true);
                _ipState = IpState::ReadData;
                return;
            }
            abortIpdMatch_(IpState::ReadLen, c);
            return;
        case IpState::ReadData:
            pushRx_((uint8_t)c, true);
            if (++_ipRead >= _ipLen) {
                _at.uart().setDataMode(false);
                _ipState = IpState::Idle;
                _rawLineLen = 0; // drop any accidental "+IPD,n:" residue from match path
            }
            // Always return: last payload byte must not fall through to the raw sniffer.
            return;
        default:
            _ipState = IpState::Idle;
            break;
    }

    feedRawByte_(c);
}

void Sim800TcpTransport::pushRx_(uint8_t b, bool fromIpd) {
    if (discardingTcpPayload_(fromIpd)) return;
    if (_rxCount >= RX_SIZE) {
        if (!_rxOverflow) {
            _rxOverflow = true;
            logger.log("[Sim800Tcp] RX overflow — MQTT stream desync risk\n");
        }
        return; // do not drop-oldest (that desyncs MQTT framing)
    }
    const uint16_t idx = (uint16_t)((_rxHead + _rxCount) % RX_SIZE);
    _rx[idx] = b;
    _rxCount++;
}

int Sim800TcpTransport::available() const {
    return (int)_rxCount;
}

int Sim800TcpTransport::read() {
    if (_rxCount == 0) return -1;
    const uint8_t b = _rx[_rxHead];
    _rxHead = (uint16_t)((_rxHead + 1) % RX_SIZE);
    _rxCount--;
    return (int)b;
}

int Sim800TcpTransport::peek() const {
    if (_rxCount == 0) return -1;
    return (int)_rx[_rxHead];
}

size_t Sim800TcpTransport::write(const uint8_t* data, size_t len) {
    if (!data || len == 0) return 0;
    // Refuse append while a CIPSEND epoch owns the TX buffer (length was frozen in AT+CIPSEND=N).
    if (_sendInProgress || _modemTxLocked || _flushRequested) return 0;
    const size_t room = TX_SIZE - _txLen;
    const size_t n = (len < room) ? len : room;
    memcpy(_tx + _txLen, data, n);
    _txLen += (uint16_t)n;
    return n;
}

bool Sim800TcpTransport::flushSend() {
    if (_txLen == 0) return false;
    _flushRequested = true;
    if (_connected && !_sendInProgress && !_modemTxLocked) {
        startSend_();
    }
    return true;
}

void Sim800TcpTransport::startSend_() {
    if (_sendInProgress) return;
    if (_txLen == 0) return;
    // Do not start CIPSEND while +IPD framing owns the UART byte stream —
    // interleaving AT+CIPSEND with mid-payload ReadData risks truncating inbound MQTT.
    if (_ipState != IpState::Idle) return;
    _sendLen = _txLen;
    // CIPMUX=0: CIPSEND without link id.
    snprintf(_cmdSend, sizeof(_cmdSend), "AT+CIPSEND=%u", (unsigned)_sendLen);
    // Expect prompt; when got prompt, bytes will be written using frozen _sendLen.
    if (!_at.enqueue({ _cmdSend, 5000, atExpectMask(AtSession::Expect::Prompt), nullptr, "CIPSEND" })) {
        logger.log("[Sim800Tcp] CIPSEND enqueue failed\n");
        _sendLen = 0;
        return;
    }
    _sendInProgress = true;
    _modemTxLocked = true; // held until SEND OK / FAIL / watchdog / recover
    _sendWatchMs = _lastNowMs ? _lastNowMs : millis(); // covers prompt wait; refreshed after '>'
}

int Sim800ClientAdapter::connect(IPAddress ip, uint16_t port) {
    char host[16];
    snprintf(host, sizeof(host), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return connect(host, port);
}

int Sim800ClientAdapter::connect(const char* host, uint16_t port) {
    // Non-blocking: kick CIPSTART once; FSM polls connected() each tick.
    if (_t.isConnected()) return 1;
    if (!_t.isConnecting()) {
        (void)_t.connectStart(host, port);
    }
    return 0;
}

size_t Sim800ClientAdapter::write(const uint8_t* buf, size_t size) {
    pump_();
    // Buffer only — CIPSEND starts on flush() after a full MQTT packet is staged.
    return _t.write(buf, size);
}

int Sim800ClientAdapter::read(uint8_t* buf, size_t size) {
    if (!buf || size == 0) return 0;
    pump_();
    size_t n = 0;
    while (n < size) {
        int c = _t.read();
        if (c < 0) break;
        buf[n++] = (uint8_t)c;
    }
    return (int)n;
}
