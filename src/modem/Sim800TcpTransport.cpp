#include "modem/Sim800TcpTransport.h"
#include <stdio.h>
#include <string.h>
#include "common/Constants.h"
#include "common/Logger.h"
#include "modem/AtParam.h"

namespace {
constexpr bool kTcpWantVerbose = Sim800Tcp::TCP_VERBOSE_LOG;
} // namespace

// Строки хода TCP пишутся всегда; болтовня +IPD/CIPSTART — только при Sim800Tcp::TCP_VERBOSE_LOG.

void Sim800TcpTransport::clearIpConfigFlags_() {
    _ipConfigDone = false;
    _ipConfigEnqueued = false;
    _ipConfigOkMask = 0;
}

void Sim800TcpTransport::resetRx_() {
    _ipd.reset();
    _ipdDiscardExit = false;
    _ipdLastByteMs = 0;
    _rxHead = 0;
    _rxCount = 0;
    _rxOverflow = false;
    _rawLineLen = 0;
    _promptLeak = 0;
    _at.uart().setDataMode(false);
    _at.uart().clearPartialLine();
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
    _port = 0;
    _host[0] = '\0';
    _cmdStart[0] = '\0';
    _cmdSend[0] = '\0';
    _txLen = 0;
    _sendLen = 0;
    resetRx_();
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
        "RX parse (%s): ipState=%u ipLen=%u ipRead=%u rxRing=%u send=%u txLock=%u conn=%u ciphead=%u\n",
        why ? why : "?", (unsigned)snap.ipState, (unsigned)snap.ipLen, (unsigned)snap.ipRead,
        (unsigned)snap.rxCount, (unsigned)snap.sendInProgress, (unsigned)snap.modemTxLocked,
        (unsigned)snap.connected, (unsigned)snap.ipConfigDone);
}

bool Sim800TcpTransport::discardingTcpPayload_(bool fromIpd) const {
    // Нужен CIPHEAD=1 (см. startConnect_): кадровый +IPD — настоящий TCP, его надо принять и посреди CIPSEND.
    // Сырые байты (не +IPD) в эпоху отправки — эхо/URC модема, в RX MQTT их не пускать.
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
            logger.log(LogModule::Sim800TcpTransport, "stack recovery CIPSHUT (%s)\n", reason ? reason : "?");
        } else {
            logger.log(LogModule::Sim800TcpTransport, "stack recovery: CIPSHUT not queued (%s)\n", reason ? reason : "?");
        }
    }
    if (_stackRecoverCount < 255) _stackRecoverCount++;
    if (_stackRecoverCount >= Sim800Tcp::STACK_RECOVER_REATTACH_THRESHOLD) {
        _needsBearerReattach = true;
        logger.log(LogModule::Sim800TcpTransport, "stack recovery threshold -> bearer reconnect\n");
    }
}

bool Sim800TcpTransport::connectStart(const char* host, uint16_t port) {
    if (!host || !host[0]) return false;
    if (_connecting || _connected) return false;
    // Не наслаивать CIPSTART поверх CIPCLOSE/CIPSHUT, AT в полёте или эпохи отправки.
    if (_closeQueued || _recoverQueued) return false;
    if (_modemTxLocked || _sendInProgress || _txLen != 0) return false;
    if (_at.isBusy() || _at.hasResult()) return false;

    const uint32_t now = millis();
    if (_lastConnectAttemptMs != 0 &&
        (int32_t)(now - _lastConnectAttemptMs) < (int32_t)Sim800Tcp::CONNECT_RETRY_COOLDOWN_MS) {
        return false;
    }

    if (!host || !host[0] || !at_param::quotedOk(host)) {
        logger.log(LogModule::Sim800TcpTransport, "connectStart: bad host\n");
        return false;
    }
    strncpy(_host, host, sizeof(_host) - 1);
    _host[sizeof(_host) - 1] = '\0';
    _port = port;
    _connecting = true;
    _connectStartMs = now;
    _lastConnectAttemptMs = now;
    clearTx_(); // не протечь нагрузкой прошлого CIPSEND в новый сеанс TCP
    logger.logSerialOnly(LogModule::Sim800TcpTransport, "connectStart host=%s port=%u\n", _host, (unsigned)_port);
    if (!startConnect_()) {
        _connecting = false;
        _connectStartMs = 0;
        logger.log(LogModule::Sim800TcpTransport, "connectStart not queued\n");
        return false;
    }
    return true;
}

void Sim800TcpTransport::stop(const char* reason) {
    const bool wasConnectedOrConnecting = (_connected || _connecting);
    _connected = false;
    _connecting = false;
    // Срыв посреди CIPSEND: снять эпоху отправки, чтобы tcpEpochBusy не блокировал переподключение MQTT навсегда.
    endSendEpoch_();
    resetRx_();
    // Пауза, чтобы следующий тик MQTT не устроил шторм CIPSTART.
    _lastConnectAttemptMs = nowMs_();
    if (reason && reason[0]) {
        logger.log(LogModule::Sim800TcpTransport, "stop(): %s\n", reason);
    } else {
        logger.logSerialOnly(LogModule::Sim800TcpTransport, "calling stop()\n");
    }
    // CIPCLOSE только если сокет ещё считается живым (URC CLOSED его уже снёс).
    if (wasConnectedOrConnecting && !_closeQueued) {
        _closeQueued = true;
        // CIPMUX=0 (одно соединение): CIPCLOSE без id канала.
        AtSession::Request r{ "AT+CIPCLOSE", Sim800Tcp::AT_CONFIG_TIMEOUT_MS, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPCLOSE" };
        (void)_at.enqueue(r);
    }
}

void Sim800TcpTransport::noteExternalCipShut() {
    // Каскад синхронизации времени владеет AT+CIPSHUT (тег TIMESHUT) — сбросить кэш прогретого CIPSTART.
    clearIpConfigFlags_();
    _didInitialCipShut = false;
    _connected = false;
    _connecting = false;
    _closeQueued = false;
    endSendEpoch_();
    logger.log(LogModule::Sim800TcpTransport, "noteExternalCipShut (time sync)\n");
}

bool Sim800TcpTransport::enqueueIpConfig_() {
    // Порядок: CIPRXGET=0 → CIPHEAD=1 → CIPMUX=0. CIPHEAD нужен для discardingTcpPayload_.
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
    logger.log(LogModule::Sim800TcpTransport, "IP config queued (waiting CIPHEAD OK)\n");
    return true;
}

bool Sim800TcpTransport::enqueueCipStart_() {
    // CIPMUX=0: CIPSTART без id канала.
    snprintf(_cmdStart, sizeof(_cmdStart), "AT+CIPSTART=\"TCP\",\"%s\",%u", _host, (unsigned)_port);
    if (!_at.enqueue({ _cmdStart, Sim800Tcp::CIPSTART_ACCEPT_TIMEOUT_MS, atExpectMask(AtSession::Expect::Ok),
                       nullptr, "CIPSTART" })) {
        return false;
    }
    return true;
}

bool Sim800TcpTransport::startConnect_() {
    // Прогретый модем после перезагрузки только ESP может держать устаревший сеанс TCP — снять один раз.
    if (!_didInitialCipShut) {
        if (!_at.enqueue({ "AT+CIPSHUT", Sim800Tcp::CIPSHUT_TIMEOUT_MS, atExpectMask(AtSession::Expect::Ok), nullptr, "CIPSHUT" })) {
            return false;
        }
        _didInitialCipShut = true;
        clearIpConfigFlags_();
        logger.log(LogModule::Sim800TcpTransport, "CIPSHUT (first, safe on warm start)\n");
        // CIPSTART после CIPSHUT OK → consumeAtResult продолжает конфиг.
        return true;
    }

    if (!_ipConfigDone) {
        if (!_ipConfigEnqueued) {
            if (!enqueueIpConfig_()) return false;
        }
        // Ждать все три бита OK до CIPSTART (см. consumeAtResult).
        return true;
    }

    return enqueueCipStart_();
}

void Sim800TcpTransport::tick(uint32_t nowMs) {
    _lastNowMs = nowMs;
    _at.tick(nowMs);

    if ((_ipd.inDiscard() || _ipd.inPayload()) && _ipdLastByteMs != 0 &&
        (nowMs - _ipdLastByteMs) >= Sim800Tcp::IPD_IDLE_TIMEOUT_MS) {
        logger.log(LogModule::Sim800TcpTransport, "IPD idle timeout — reset RX\n");
        resetRx_();
    }

    // Сторожа соединения и отправки (зависание модема / застрявшие флаги).
    if (_connecting && _connectStartMs != 0 &&
        (nowMs - _connectStartMs) >= Sim800Tcp::CONNECT_WATCHDOG_MS) {
        logger.log(LogModule::Sim800TcpTransport, "connect watchdog\n");
        _connecting = false;
        _connected = false;
        _connectStartMs = 0;
        endSendEpoch_();
        resetRx_();
        forceStackRecover_("connect_wd");
    }
    if (_sendInProgress && _sendWatchMs != 0 &&
        (nowMs - _sendWatchMs) >= Sim800Tcp::SEND_WATCHDOG_MS) {
        logger.log(LogModule::Sim800TcpTransport, "send watchdog\n");
        endSendEpoch_();
        _connected = false;
        _connecting = false;
        resetRx_();
        forceStackRecover_("send_wd");
    }

    // Done у AtSession забирает только GsmController::drainAtResult_ (забрать → потребить → поглотить).

    // Начать запечатанный CIPSEND, если уже звали flushSend().
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
        _modemTxLocked = false; // восстановление кончилось — снять замок эпохи отправки
        clearIpConfigFlags_();
        // После прогретого CIPSHUT (или восстановления) снова выставить CIPHEAD до CIPSTART.
        if (_connecting && !_ipConfigDone) {
            if (!enqueueIpConfig_()) {
                _connecting = false;
                logger.log(LogModule::Sim800TcpTransport, "IP config not queued after CIPSHUT\n");
            }
        }
        return true;
    }
    if (strcmp(r.tag, "CIPSTART") == 0) {
        // Команда принята; само соединение сигналит URC CONNECT OK / CONNECT FAIL.
        if (r.timedOut || r.error) {
            logger.log(LogModule::Sim800TcpTransport, "CIPSTART not accepted (timeout=%u error=%u)\n",
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
            // CIPSEND=<len>: послать ровно длину, зафиксированную в startSend_ (в режиме длины нет Ctrl+Z).
            const uint16_t n = (_sendLen <= _txLen) ? _sendLen : _txLen;
            if (n) _at.uart().writeBytes(_tx, n);
            _sendWatchMs = _lastNowMs ? _lastNowMs : millis(); // ждать SEND OK / SEND FAIL
            // Держать _modemTxLocked и _sendInProgress до SEND OK (после '>' AtSession уже Idle).
        } else if (r.timedOut || r.error) {
            logger.log(LogModule::Sim800TcpTransport, "CIPSEND not accepted (timeout=%u error=%u)\n",
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
            logger.log(LogModule::Sim800TcpTransport, "IP config failed label=%s timeout=%u error=%u ok=%u\n", r.tag,
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
            logger.log(LogModule::Sim800TcpTransport, "IP config ok (CIPHEAD=1 confirmed)\n");
            if (_connecting) {
                if (!enqueueCipStart_()) {
                    logger.log(LogModule::Sim800TcpTransport, "CIPSTART not queued after IP config\n");
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
    // URC соединения — голый ERROR не считать им (UART общий с AT GSM).
    if (strstr(line, "CONNECT OK") != nullptr) {
        _lastConnectOkMs = _lastNowMs;
        logger.log(LogModule::Sim800TcpTransport, "TCP connected\n");
        _connected = true;
        _connecting = false;
        _connectStartMs = 0;
        endSendEpoch_(); // новый сокет — выбросить подготовку до соединения
        resetRx_();      // не тащить хвост прошлого брокера в MQTT
        noteConnectOk_();
        return;
    }
    if (strstr(line, "ALREADY CONNECT") != nullptr) {
        logger.log(LogModule::Sim800TcpTransport, "TCP already connected\n");
        _connected = true;
        _connecting = false;
        _connectStartMs = 0;
        endSendEpoch_();
        resetRx_();
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
        resetRx_();
        // Если отправка ещё идёт, шину держать запертой, чтобы CIPSTART не обогнал конец CIPSHUT.
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
        resetRx_();
        _lastConnectAttemptMs = now;
        forceStackRecover_("connect_fail");
        return;
    }
    if (strcmp(line, "SEND OK") == 0) {
        endSendEpoch_();
        return;
    }
    if (strstr(line, "SEND FAIL") != nullptr) {
        logger.log(LogModule::Sim800TcpTransport, "SEND FAIL — stack recovery\n");
        endSendEpoch_();
        _connected = false;
        _connecting = false;
        forceStackRecover_("send_fail");
        return;
    }
}

void Sim800TcpTransport::feedRawByte_(char c) {
    // Сырой путь (запасной / текст модема): фильтр управляющих строк CRLF в RX MQTT.
    // 0x20 не считать началом текста — с этого байта начинается CONNACK MQTT.
    const uint8_t ub = (uint8_t)c;
    const bool printable = (ub == '\r' || ub == '\n' || ub == '\t' || (ub >= 0x20 && ub <= 0x7E));

    // Снять утёкшее приглашение CIPSEND '>' до разделения на печать и двоичное.
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
            // Проваливаемся: первый настоящий байт TCP после одиночного '>'.
        } else if (_promptLeak == 2) {
            if (ub == '\n') {
                _promptLeak = 0;
                return;
            }
            _promptLeak = 0;
            // Проваливаемся
        }
    }

    if (!printable) {
        // Двоичный байт: накопленные байты строки сбросить как нагрузку и пропустить дальше.
        _promptLeak = 0;
        for (uint8_t i = 0; i < _rawLineLen; i++) pushRx_((uint8_t)_rawLineBuf[i]);
        _rawLineLen = 0;
        if (_connected) pushRx_(ub);
        return;
    }

    // Новая утечка приглашения: '>' нельзя копить в строку как текст модема (склеится с байтами MQTT).
    if (_connected && _rawLineLen == 0 && ub == '>') {
        _promptLeak = 1;
        return;
    }

    // Начало строки при соединении: копить только правдоподобные начала текста модема, иначе нагрузка TCP.
    if (_connected && _rawLineLen == 0) {
        if (ub == '\r' || ub == '\n') {
            return;
        }
        const bool maybeModemTextStart =
            (ub == '+') || // URC вроде +CREG, +CSQ, +IPD...
            (ub == 'A') || // эхо AT / ALREADY CONNECT
            (ub == 'O') || // OK
            (ub == 'E') || // ERROR
            (ub == 'C') || // CLOSED / CONNECT / CLOSE OK / CSQ (без '+')
            (ub == 'S') || // SEND OK/FAIL / SHUT OK
            (ub == 'R') || // RDY
            (ub == 'N') || // NO CARRIER (редко)
            (ub == 'F') || // обрывки FAIL
            (ub == 'I');   // IPD, (без '+')

        if (!maybeModemTextStart) {
            pushRx_(ub);
            return;
        }
    }

    if (_rawLineLen + 1 >= RAW_LINE_BUF_SIZE) {
        // Слишком длинно для управляющей строки: считать нагрузкой.
        for (uint8_t i = 0; i < _rawLineLen; i++) pushRx_((uint8_t)_rawLineBuf[i]);
        _rawLineLen = 0;
        if (_connected) pushRx_(ub);
        return;
    }

    _rawLineBuf[_rawLineLen++] = c;
    if (c != '\n') {
        return;
    }

    // Набрался кандидат в строку, заканчивающуюся CRLF.
    if (!sim800_lines::isControlLine(_rawLineBuf, _rawLineLen) && _connected) {
        for (uint8_t i = 0; i < _rawLineLen; i++) pushRx_((uint8_t)_rawLineBuf[i]);
    }
    _rawLineLen = 0;
}

void Sim800TcpTransport::onByte(char c) {
    // Кадрирование +IPD (CIPHEAD=1) владеет байтом на время сопоставления и чтения; остальное — текст модема.
    using Ev = sim800_lines::IpdFramer::Event;
    if (_ipdDiscardExit) {
        // Этот байт уже после заявленной длины — его можно собирать как строку.
        _at.uart().setDataMode(false);
        _rawLineLen = 0;
        _ipdDiscardExit = false;
    }
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
                _ipdLastByteMs = 0;
                return;
            }
            _at.uart().setDataMode(true);
            _ipdLastByteMs = nowMs_();
            return;
        case Ev::Payload:
            _ipdLastByteMs = nowMs_();
            pushRx_((uint8_t)c, true);
            return;
        case Ev::PayloadEnd:
            pushRx_((uint8_t)c, true);
            _at.uart().setDataMode(false);
            _rawLineLen = 0; // выбросить случайный остаток "+IPD,n:" с пути сопоставления
            _ipdLastByteMs = 0;
            return;
        case Ev::Discard:
            // Иначе UART склеит хвост кадра со следующим OK.
            _at.uart().setDataMode(true);
            _ipdLastByteMs = nowMs_();
            if (!_ipd.inDiscard()) {
                _ipdDiscardExit = true;
                _ipdLastByteMs = 0;
            }
            return;
        case Ev::Raw:
            // Повторить байты, съеденные сорванным заголовком, чтобы URC вроде +CSQ сохранили '+'.
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
            logger.log(LogModule::Sim800TcpTransport, "RX overflow — risk of MQTT stream desync\n");
        }
        return; // не выбрасывать старейшее (это рассинхронизирует кадры MQTT)
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
    // Не дописывать, пока эпоха CIPSEND владеет буфером TX (длина уже зафиксирована в AT+CIPSEND=N).
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
    // Не начинать CIPSEND, пока кадрирование +IPD владеет потоком байт UART —
    // чередование AT+CIPSEND с ReadData посреди нагрузки может обрезать входящий MQTT.
    if (_ipd.state() != sim800_lines::IpdFramer::State::Idle) return;
    _sendLen = _txLen;
    // CIPMUX=0: CIPSEND без id канала.
    snprintf(_cmdSend, sizeof(_cmdSend), "AT+CIPSEND=%u", (unsigned)_sendLen);
    // Ждать приглашение; когда оно придёт, байты пишутся по зафиксированному _sendLen.
    if (!_at.enqueue({ _cmdSend, Sim800Tcp::CIPSEND_PROMPT_TIMEOUT_MS, atExpectMask(AtSession::Expect::Prompt), nullptr, "CIPSEND" })) {
        logger.log(LogModule::Sim800TcpTransport, "CIPSEND not queued\n");
        _sendLen = 0;
        return;
    }
    _sendInProgress = true;
    _modemTxLocked = true; // держится до SEND OK / FAIL / watchdog / recover
    _sendWatchMs = _lastNowMs ? _lastNowMs : millis(); // покрывает ожидание приглашения; обновляется после '>'
}

int Sim800ClientAdapter::connect(IPAddress ip, uint16_t port) {
    char host[16];
    snprintf(host, sizeof(host), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return connect(host, port);
}

int Sim800ClientAdapter::connect(const char* host, uint16_t port) {
    // Неблокирующе: один раз запустить CIPSTART; FSM опрашивает connected() каждый тик.
    if (_t.isConnected()) return 1;
    if (!_t.isConnecting()) {
        (void)_t.connectStart(host, port);
    }
    return 0;
}

size_t Sim800ClientAdapter::write(const uint8_t* buf, size_t size) {
    pump_();
    // Только буфер — CIPSEND начинается в flush(), когда целый пакет MQTT подготовлен.
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
