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
    _ipd.reset();
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
    if (_ipd.inPayload()) {
        _at.uart().setDataMode(false);
    }
    _ipd.reset();
}

bool Sim800TcpTransport::takeRxOverflow() {
    if (!_rxOverflow) return false;
    _rxOverflow = false;
    return true;
}

void Sim800TcpTransport::fillRxForensic(RxForensic& out) const {
    out.ipState = static_cast<uint8_t>(_ipd.state());
    out.ipLen = _ipd.length();
    out.ipRead = _ipd.received();
    out.rxCount = _rxCount;
    out.sendInProgress = _sendInProgress;
    out.modemTxLocked = _modemTxLocked;
    out.connected = _connected;
    out.ipConfigDone = _ipConfigDone;
}

void Sim800TcpTransport::logRxForensic(const char* why) const {
    RxForensic snap;
    fillRxForensic(snap);
    logger.log(LogModule::Sim800TcpTransport,
        "RX forensic (%s): ipState=%u ipLen=%u ipRead=%u rxRing=%u "
        "send=%u txLock=%u conn=%u ciphead=%u\n",
        why ? why : "?", (unsigned)snap.ipState, (unsigned)snap.ipLen, (unsigned)snap.ipRead,
        (unsigned)snap.rxCount, (unsigned)snap.sendInProgress, (unsigned)snap.modemTxLocked,
        (unsigned)snap.connected, (unsigned)snap.ipConfigDone);
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
        if (_at.enqueueHigh({ "AT+CIPSHUT", Sim800Tcp::CIPSHUT_TIMEOUT_MS, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPSHUT" })) {
            _recoverQueued = true;
            logger.log(LogModule::Sim800TcpTransport, "stack recover CIPSHUT (%s)\n", reason ? reason : "?");
        } else {
            logger.log(LogModule::Sim800TcpTransport, "stack recover CIPSHUT enqueue failed (%s)\n", reason ? reason : "?");
        }
    }
    if (_stackRecoverCount < 255) _stackRecoverCount++;
    if (_stackRecoverCount >= Sim800Tcp::STACK_RECOVER_REATTACH_THRESHOLD) {
        _needsBearerReattach = true;
        logger.log(LogModule::Sim800TcpTransport, "stack recover threshold -> bearer reattach\n");
    }
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
    logger.logSerialOnly(LogModule::Sim800TcpTransport, "connectStart host=%s port=%u\n", _host, (unsigned)_port);
    if (!startConnect_()) {
        _connecting = false;
        _connectStartMs = 0;
        logger.log(LogModule::Sim800TcpTransport, "connectStart enqueue failed\n");
        return false;
    }
    return true;
}

void Sim800TcpTransport::stop(const char* reason) {
    const bool wasConnectedOrConnecting = (_connected || _connecting);
    _connected = false;
    _connecting = false;
    // Abort mid-CIPSEND: clear send-epoch so tcpEpochBusy cannot block MQTT reconnect forever.
    endSendEpoch_();
    _promptLeak = 0;
    // Cooldown so the next MQTT tick does not CIPSTART-storm.
    _lastConnectAttemptMs = nowMs_();
    if (reason && reason[0]) {
        logger.log(LogModule::Sim800TcpTransport, "stop(): %s\n", reason);
    } else {
        logger.logSerialOnly(LogModule::Sim800TcpTransport, "stop()\n");
    }
    // Only CIPCLOSE if we still believe a socket exists (CLOSED URC already tore it down).
    if (wasConnectedOrConnecting && !_closeQueued) {
        _closeQueued = true;
        // CIPMUX=0 (single connection): CIPCLOSE without link id.
        AtSession::Request r{ "AT+CIPCLOSE", Sim800Tcp::AT_CONFIG_TIMEOUT_MS, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPCLOSE" };
        (void)_at.enqueue(r);
    }
}

void Sim800TcpTransport::noteExternalCipShut() {
    // Time-sync cascade owns AT+CIPSHUT (tag TIMESHUT) — invalidate warm CIPSTART cache.
    clearIpConfigFlags_();
    _didInitialCipShut = false;
    _connected = false;
    _connecting = false;
    _closeQueued = false;
    endSendEpoch_();
    logger.log(LogModule::Sim800TcpTransport, "noteExternalCipShut (time-sync)\n");
}

bool Sim800TcpTransport::enqueueIpConfig_() {
    // Order: CIPRXGET=0 → CIPHEAD=1 → CIPMUX=0. CIPHEAD is required for discardingTcpPayload_.
    if (_at.normalQueueFree() < 3) return false;
    if (!_at.enqueue({ "AT+CIPRXGET=0", Sim800Tcp::AT_CONFIG_TIMEOUT_MS, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPRXGET0" })) {
        return false;
    }
    if (!_at.enqueue({ "AT+CIPHEAD=1", Sim800Tcp::AT_CONFIG_TIMEOUT_MS, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPHEAD1" })) {
        return false;
    }
    if (!_at.enqueue({ "AT+CIPMUX=0", Sim800Tcp::AT_CONFIG_TIMEOUT_MS, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPMUX0" })) {
        return false;
    }
    _ipConfigEnqueued = true;
    _ipConfigOkMask = 0;
    _ipConfigDone = false;
    logger.log(LogModule::Sim800TcpTransport, "IP config enqueue (await CIPHEAD OK)\n");
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
        if (!_at.enqueue({ "AT+CIPSHUT", Sim800Tcp::CIPSHUT_TIMEOUT_MS, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPSHUT" })) {
            return false;
        }
        _didInitialCipShut = true;
        clearIpConfigFlags_();
        logger.log(LogModule::Sim800TcpTransport, "CIPSHUT (initial, warm-safe)\n");
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
        logger.log(LogModule::Sim800TcpTransport, "connect watchdog\n");
        _connecting = false;
        _connected = false;
        _connectStartMs = 0;
        endSendEpoch_();
        forceStackRecover_("connect_wd");
    }
    if (_sendInProgress && _sendWatchMs != 0 &&
        (nowMs - _sendWatchMs) >= Sim800Tcp::SEND_WATCHDOG_MS) {
        logger.log(LogModule::Sim800TcpTransport, "send watchdog\n");
        endSendEpoch_();
        _connected = false;
        _connecting = false;
        forceStackRecover_("send_wd");
    }

    // AtSession Done is drained only by GsmController::drainAtResult_ (take → consume → absorb).

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
                logger.log(LogModule::Sim800TcpTransport, "IP config enqueue failed after CIPSHUT\n");
            }
        }
        return true;
    }
    if (strcmp(r.tag, "CIPSTART") == 0) {
        // Command acceptance; actual connect is signaled via URC CONNECT OK / CONNECT FAIL.
        if (r.timedOut || r.error) {
            logger.log(LogModule::Sim800TcpTransport, "CIPSTART accept failed (timeout=%u error=%u)\n",
                       (unsigned)r.timedOut, (unsigned)r.error);
            _connecting = false;
            _connected = false;
            _connectStartMs = 0;
            _lastConnectAttemptMs = _lastNowMs ? _lastNowMs : millis();
            forceStackRecover_("cipstart_accept");
        } else if (r.ok) {
            if (kTcpWantVerbose) {
                logger.log(LogModule::Sim800TcpTransport, "CIPSTART accepted (waiting URC)\n");
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
            logger.log(LogModule::Sim800TcpTransport, "CIPSEND accept failed (timeout=%u error=%u)\n",
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
            logger.log(LogModule::Sim800TcpTransport, "IP config fail tag=%s timeout=%u error=%u ok=%u\n", r.tag,
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
            logger.log(LogModule::Sim800TcpTransport, "IP config OK (CIPHEAD=1 confirmed)\n");
            if (_connecting) {
                if (!enqueueCipStart_()) {
                    logger.log(LogModule::Sim800TcpTransport, "CIPSTART enqueue failed after IP config\n");
                    _connecting = false;
                    forceStackRecover_("cipstart_after_cfg");
                }
            }
        }
        return true;
    }
    return false;
}

void Sim800TcpTransport::onLine(const char* line) {
    if (!line || !line[0]) return;
    // Connection URCs — do not treat bare ERROR (shared UART with GSM AT).
    if (strstr(line, "CONNECT OK") != nullptr) {
        _lastConnectOkMs = _lastNowMs;
        logger.log(LogModule::Sim800TcpTransport, "TCP connected\n");
        _connected = true;
        _connecting = false;
        _connectStartMs = 0;
        endSendEpoch_(); // new socket — drop any pre-connect staging
        noteConnectOk_();
        return;
    }
    if (strstr(line, "ALREADY CONNECT") != nullptr) {
        logger.log(LogModule::Sim800TcpTransport, "TCP already connected\n");
        _connected = true;
        _connecting = false;
        _connectStartMs = 0;
        endSendEpoch_();
        noteConnectOk_();
        return;
    }
    if (strcmp(line, "CLOSED") == 0) {
        const uint32_t dt = (_lastConnectOkMs != 0) ? (_lastNowMs - _lastConnectOkMs) : 0;
        logger.log(LogModule::Sim800TcpTransport, "TCP closed (uptime=%ums)\n", (unsigned)dt);
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
            logger.log(LogModule::Sim800TcpTransport, "TCP connect failed\n");
        } else {
            logger.logSerialOnly(LogModule::Sim800TcpTransport, "TCP connect failed\n");
        }
        _connected = false;
        _connecting = false;
        _connectStartMs = 0;
        endSendEpoch_();
        _lastConnectAttemptMs = now;
        forceStackRecover_("connect_fail");
        return;
    }
    if (strcmp(line, "SEND OK") == 0) {
        endSendEpoch_();
        return;
    }
    if (strstr(line, "SEND FAIL") != nullptr) {
        logger.log(LogModule::Sim800TcpTransport, "SEND FAIL — stack recover\n");
        endSendEpoch_();
        _connected = false;
        _connecting = false;
        forceStackRecover_("send_fail");
        return;
    }
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
    if (!sim800_lines::isControlLine(_rawLineBuf, _rawLineLen) && _connected) {
        for (uint8_t i = 0; i < _rawLineLen; i++) pushRx_((uint8_t)_rawLineBuf[i]);
    }
    _rawLineLen = 0;
}

void Sim800TcpTransport::onByte(char c) {
    // +IPD framing (CIPHEAD=1) owns the byte while matching/reading; everything else is modem text.
    using Ev = sim800_lines::IpdFramer::Event;
    switch (_ipd.feed(c)) {
        case Ev::Consumed:
            return;
        case Ev::DataStart:
            if (kTcpWantVerbose) {
                logger.log(LogModule::Sim800TcpTransport, "+IPD len=%u\n", (unsigned)_ipd.length());
            }
            if (_ipd.length() == 0) {
                _at.uart().setDataMode(false);
                _rawLineLen = 0;
                return;
            }
            _at.uart().setDataMode(true);
            return;
        case Ev::Payload:
            pushRx_((uint8_t)c, true);
            return;
        case Ev::PayloadEnd:
            pushRx_((uint8_t)c, true);
            _at.uart().setDataMode(false);
            _rawLineLen = 0; // drop any accidental "+IPD,n:" residue from the match path
            return;
        case Ev::Raw:
            // Replay bytes eaten by an aborted header so URCs like +CSQ keep their '+'.
            for (const char* p = _ipd.replay(); *p; ++p) feedRawByte_(*p);
            feedRawByte_(c);
            return;
    }
}

void Sim800TcpTransport::pushRx_(uint8_t b, bool fromIpd) {
    if (discardingTcpPayload_(fromIpd)) return;
    if (_rxCount >= RX_SIZE) {
        if (!_rxOverflow) {
            _rxOverflow = true;
            logger.log(LogModule::Sim800TcpTransport, "RX overflow — MQTT stream desync risk\n");
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
    if (_ipd.state() != sim800_lines::IpdFramer::State::Idle) return;
    _sendLen = _txLen;
    // CIPMUX=0: CIPSEND without link id.
    snprintf(_cmdSend, sizeof(_cmdSend), "AT+CIPSEND=%u", (unsigned)_sendLen);
    // Expect prompt; when got prompt, bytes will be written using frozen _sendLen.
    if (!_at.enqueue({ _cmdSend, Sim800Tcp::CIPSEND_PROMPT_TIMEOUT_MS, atExpectMask(AtSession::Expect::Prompt), nullptr, "CIPSEND" })) {
        logger.log(LogModule::Sim800TcpTransport, "CIPSEND enqueue failed\n");
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
