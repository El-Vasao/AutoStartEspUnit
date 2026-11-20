#include "mqtt/MqttFsmClient.h"

#include "common/Constants.h"
#include "common/Logger.h"
#include "mqtt/MqttWire.h"

#include <stdio.h>
#include <string.h>

namespace {

class JsonIntoBufferPrint : public Print {
public:
    JsonIntoBufferPrint(uint8_t* buf, size_t cap) : _buf(buf), _cap(cap) {}

    size_t write(uint8_t c) override {
        if (_n >= _cap) {
            overflow_ = true;
            return 0;
        }
        _buf[_n++] = c;
        return 1;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        size_t w = 0;
        while (w < size) {
            if (_n >= _cap) {
                overflow_ = true;
                break;
            }
            _buf[_n++] = buffer[w++];
        }
        return w;
    }

    size_t len() const { return _n; }
    bool overflowed() const { return overflow_; }
    void reset() {
        _n = 0;
        overflow_ = false;
    }

private:
    uint8_t* _buf;
    size_t _cap;
    size_t _n{0};
    bool overflow_{false};
};

} // namespace

// Стыкуется с Sim800Tcp::CONNECT_WATCHDOG_MS (15 с): транспорт восстанавливает модем раньше, FSM сдаётся позже.
static constexpr uint32_t kTcpConnectTimeoutMs = NetTiming::MQTT_TCP_CONNECT_TIMEOUT_MS;
static constexpr uint32_t kMqttConnectTimeoutMs = NetTiming::MQTT_FSM_CONNECT_TIMEOUT_MS;

MqttFsmClient::MqttFsmClient(Client& netClient)
    : _net(netClient) {}

void MqttFsmClient::begin(const Config& cfg) {
    _cfg = cfg;
    resetSession_();
    _state = State::Idle;
}

void MqttFsmClient::requestConnect() {
    _connectRequested = true;
    _disconnectRequested = false;
}

void MqttFsmClient::requestDisconnect() {
    _disconnectRequested = true;
    _connectRequested = false;
}

void MqttFsmClient::resetSession_() {
    clearTxQueue_();
    clearQos1Reply_();
    _rxLen = 0;
    _rxAssembleStartMs = 0;
    _tcpConnectStartMs = 0;
    _mqttHandshakeStartMs = 0;
    _lastRxMs = 0;
    _lastTxMs = 0;
    _lastPingMs = 0;
    _subRequested = false;
    _awaitingSuback = false;
    _subscribeConfirmed = false;
    _subPacketId = 0;
    _subTopic[0] = '\0';
}

void MqttFsmClient::clearQos1Reply_() {
    _qos1ReplyActive = false;
    _qos1ReplyDupSent = false;
    _qos1ReplyPacketId = 0;
    _qos1ReplyLen = 0;
    _qos1ReplySentMs = 0;
}

uint16_t MqttFsmClient::allocPacketId_() {
    const uint16_t pid = _nextPacketId ? _nextPacketId : 1;
    _nextPacketId = (uint16_t)(pid == 0xFFFF ? 1 : pid + 1);
    return pid;
}

void MqttFsmClient::forceError(const char* reason) {
    setError_(reason ? reason : "force");
}

void MqttFsmClient::clearTxQueue_() {
    _txQHead = 0;
    _txQTail = 0;
    _txQCount = 0;
    _txOff = 0;
    for (uint8_t i = 0; i < TX_Q_DEPTH; i++) {
        _txQLen[i] = 0;
        _txQClass[i] = OutClass::Ctrl;
        _txQTeleKind[i] = TeleKind::None;
    }
}

int MqttFsmClient::findReplaceableDeltaTele_() const {
    for (uint8_t i = 0; i < _txQCount; i++) {
        const uint8_t idx = (uint8_t)((_txQHead + i) % TX_Q_DEPTH);
        if (_txQClass[idx] != OutClass::Tele) continue;
        if (_txQTeleKind[idx] != TeleKind::Delta) continue;
        if (i == 0 && _txOff != 0) continue; // голова в середине отправки
        return (int)idx;
    }
    return -1;
}

int MqttFsmClient::findReplaceableFullTele_() const {
    for (uint8_t i = 0; i < _txQCount; i++) {
        const uint8_t idx = (uint8_t)((_txQHead + i) % TX_Q_DEPTH);
        if (_txQClass[idx] != OutClass::Tele) continue;
        if (_txQTeleKind[idx] != TeleKind::Full) continue;
        if (i == 0 && _txOff != 0) continue;
        return (int)idx;
    }
    return -1;
}

bool MqttFsmClient::hasCtrlInQueue_() const {
    for (uint8_t i = 0; i < _txQCount; i++) {
        const uint8_t idx = (uint8_t)((_txQHead + i) % TX_Q_DEPTH);
        if (_txQClass[idx] == OutClass::Ctrl) return true;
    }
    return false;
}

bool MqttFsmClient::hasCtrlOutbound() const {
    return hasCtrlInQueue_();
}

bool MqttFsmClient::hasFullTeleOutbound() const {
    for (uint8_t i = 0; i < _txQCount; i++) {
        const uint8_t idx = (uint8_t)((_txQHead + i) % TX_Q_DEPTH);
        if (_txQClass[idx] == OutClass::Tele && _txQTeleKind[idx] == TeleKind::Full) return true;
    }
    return false;
}

bool MqttFsmClient::beginQueueSlot_(OutClass cls, uint8_t*& buf, uint16_t& cap) {
    if (cls == OutClass::Tele) {
        // Низший приоритет: не брать новый слот Tele, пока ждёт Ctrl.
        if (hasCtrlInQueue_()) return false;
        // После постановки в очередь оставить ≥1 свободный слот для Ctrl.
        if (freeSlots_() < 2) return false;
    } else if (!queueHasRoom_()) {
        return false;
    }
    buf = _txQ[_txQTail];
    cap = TX_MAX;
    return true;
}

void MqttFsmClient::commitQueueSlot_(uint16_t len, OutClass cls, TeleKind teleKind) {
    if (len == 0 || len > TX_MAX) return;
    _txQLen[_txQTail] = len;
    _txQClass[_txQTail] = cls;
    _txQTeleKind[_txQTail] = (cls == OutClass::Tele) ? teleKind : TeleKind::None;
    _txQTail = (uint8_t)((_txQTail + 1u) % TX_Q_DEPTH);
    _txQCount++;
}

bool MqttFsmClient::beginReplaceTeleSlot_(TeleKind wantKind, uint8_t*& buf, uint16_t& cap, uint8_t& slotIdxOut) {
    int idx = -1;
    if (wantKind == TeleKind::Delta) {
        idx = findReplaceableDeltaTele_();
    } else if (wantKind == TeleKind::Full) {
        idx = findReplaceableFullTele_();
        if (idx < 0) idx = findReplaceableDeltaTele_(); // Full может заменить ждущую Delta
    }
    if (idx < 0) return false;
    slotIdxOut = (uint8_t)idx;
    buf = _txQ[slotIdxOut];
    cap = TX_MAX;
    return true;
}

void MqttFsmClient::commitReplaceTeleSlot_(uint8_t slotIdx, uint16_t len, TeleKind kind) {
    if (slotIdx >= TX_Q_DEPTH || len == 0 || len > TX_MAX) return;
    _txQLen[slotIdx] = len;
    _txQClass[slotIdx] = OutClass::Tele;
    _txQTeleKind[slotIdx] = kind;
}

void MqttFsmClient::setError_(const char* reason) {
    _lastErrorReason = reason ? reason : "?";
    logger.log(LogModule::MqttFsmClient, "closing tcp: %s\n", _lastErrorReason);
    _state = State::Error;
    _net.stop();
    resetSession_();
}

bool MqttFsmClient::subscribe(const char* topic) {
    if (!topic || !topic[0]) return false;
    const size_t n = strnlen(topic, sizeof(_subTopic) - 1);
    if (n == 0 || n >= sizeof(_subTopic)) return false;
    memcpy(_subTopic, topic, n);
    _subTopic[n] = '\0';
    _subRequested = true;
    return true;
}

void MqttFsmClient::requeueSubscribe() {
    if (!_subTopic[0]) return;
    if (_subRequested) return;
    if (_awaitingSuback && _txQCount != 0) return;
    _awaitingSuback = false;
    _subPacketId = 0;
    _subRequested = true;
}

bool MqttFsmClient::publish(const char* topic, const uint8_t* payload, uint16_t len, bool retained, bool control,
                            TeleKind teleKind, uint8_t qos) {
    if (!topic || !topic[0]) return false;
    if (!payload && len) return false;
    if (_state != State::Connected) return false;
    return buildPublish_(topic, payload, len, retained, control ? OutClass::Ctrl : OutClass::Tele, teleKind, qos);
}

void MqttFsmClient::tick(const Budgets& b) {
    _budgets = b;
    const uint32_t now = millis();
    const uint32_t deadlineMs =
        (b.maxMsPerTick > 0) ? (now + (uint32_t)b.maxMsPerTick) : 0;

    auto doWrite = [&]() { pumpWrite_(b.maxWriteBytesPerTick, deadlineMs); };
    auto doRead = [&]() {
        if (b.shouldDeferRead && b.shouldDeferRead(b.shouldDeferReadCtx)) {
            return;
        }
        pumpReadAndParse_(b.maxReadBytesPerTick, b.maxParseFramesPerTick, deadlineMs);
    };

    if (_disconnectRequested) {
        // Опустошить исходящую очередь (например, удержанное сообщение offline) до DISCONNECT и остановки TCP.
        doWrite();
        if (_txQCount != 0) return;
        if (_state == State::Connected) {
            (void)buildDisconnect_();
            doWrite();
        }
        _net.stop();
        resetSession_();
        _state = State::Idle;
        _disconnectRequested = false;
        return;
    }

    if (!_connectRequested) {
        // Пассивный режим: всё равно читать, чтобы буферы пустели, если что-то подключено.
        if (_net.connected()) {
            doRead();
            doWrite();
        }
        return;
    }

    // Сначала добиться TCP.
    if (!ensureTcp_()) return;

    // Когда TCP поднят, вести соединение MQTT, затем keepalive.
    if (_state == State::TcpConnecting) {
        // Сюда попадать не должны: переход делает ensureTcp_().
        return;
    }

    if (_state == State::MqttConnecting) {
        // Послать CONNECT один раз.
        if (_txQCount == 0 && _lastTxMs == 0) {
            if (!buildConnect_()) {
                setError_("connect_build");
                return;
            }
            if (_mqttHandshakeStartMs == 0) {
                _mqttHandshakeStartMs = millis();
            }
        }
        doWrite();
        doRead();

        // Часы рукопожатия могут встать по millis() уже после снимка `now` в этом тике —
        // защита от оборачивания uint32 вниз (мгновенный ложный mqtt_connack_timeout).
        const uint32_t hs0 = (_mqttHandshakeStartMs != 0) ? _mqttHandshakeStartMs : _tcpConnectStartMs;
        const uint32_t nowHs = millis();
        if (_state == State::MqttConnecting && hs0 != 0 && (int32_t)(nowHs - hs0) > (int32_t)kMqttConnectTimeoutMs) {
            setError_("mqtt_connack_timeout");
        }
        return;
    }

    if (_state == State::Connected) {
        // Запрос подписки (одна тема) — в очередь, когда слот свободен.
        if (_subRequested && !_awaitingSuback && queueHasRoom_()) {
            if (buildSubscribe_(_subTopic)) {
                _awaitingSuback = true;
                _subRequested = false;
            }
        }

        tickQos1Reply_(now);
        maybeSendPing_(now);
        maybeKeepaliveWatchdog_(now);
        if (_state != State::Connected) return;
        doWrite();
        doRead();
        // Обработчик cmd может поставить ответ в очередь во время doRead — вытолкнуть в этом тике.
        doWrite();

        // Транспорт отвалился (CLOSED / SEND FAIL / CIPSHUT) — считать Error для серии reattach.
        // Пропустить, если обработчик кадра уже оборвал сеанс (причина сохраняется, второй stop() не нужен).
        if (_state == State::Connected && !_net.connected()) {
            setError_("tcp_drop");
        }
    }
}

void MqttFsmClient::tickQos1Reply_(uint32_t now) {
    if (!_qos1ReplyActive) return;
    if (_qos1ReplySentMs == 0) return;
    if ((now - _qos1ReplySentMs) < NetTiming::MQTT_REPLY_PUBACK_TIMEOUT_MS) return;
    if (!_qos1ReplyDupSent && _qos1ReplyLen > 0 && queueHasRoom_()) {
        // Один DUP: переписать флаг DUP у сохранённого PUBLISH и снова поставить как Ctrl.
        uint8_t* slot = nullptr;
        uint16_t cap = 0;
        if (beginQueueSlot_(OutClass::Ctrl, slot, cap) && _qos1ReplyLen <= cap) {
            memcpy(slot, _qos1ReplyCopy, _qos1ReplyLen);
            if (_qos1ReplyLen >= 1) slot[0] = (uint8_t)(slot[0] | 0x08); // DUP
            commitQueueSlot_(_qos1ReplyLen, OutClass::Ctrl);
            _qos1ReplyDupSent = true;
            _qos1ReplySentMs = now;
            logger.log(LogModule::MqttFsmClient, "QoS1 reply DUP pid=%u\n", (unsigned)_qos1ReplyPacketId);
            return;
        }
    }
    logger.log(LogModule::MqttFsmClient, "QoS1 reply exhausted pid=%u\n", (unsigned)_qos1ReplyPacketId);
    clearQos1Reply_();
}

bool MqttFsmClient::ensureTcp_() {
    const uint32_t now = millis();
    // Переподключение после Error: снять удерживаемое Error, чтобы мог начаться Idle→TcpConnecting.
    if (_state == State::Error) {
        _state = State::Idle;
    }
    if (_state == State::Idle) {
        // Не пускать CIPSTART, пока плоскость управления модема занята (например, CNTP).
        if (_budgets.shouldBlockConnect &&
            _budgets.shouldBlockConnect(_budgets.shouldBlockConnectCtx)) {
            return false;
        }
        if (_net.connected()) {
            _tcpConnectStartMs = now;
            _mqttHandshakeStartMs = 0;
            _state = State::MqttConnecting;
            return true;
        }
        // Здесь stop() не звать: транспорт уже закрыт после setError_/CLOSED.
        resetSession_();
        _tcpConnectStartMs = now;
        _mqttHandshakeStartMs = 0;
        _state = State::TcpConnecting;
    }

    if (_state == State::TcpConnecting) {
        if (_net.connected()) {
            if (_mqttHandshakeStartMs == 0) {
                _mqttHandshakeStartMs = millis();
            }
            _state = State::MqttConnecting;
            return true;
        }
        if (!_cfg.host || !_cfg.host[0]) {
            setError_("tcp_no_host");
            return false;
        }
        // Неблокирующий пуск соединения (паузу CIPSTART держит транспорт).
        (void)_net.connect(_cfg.host, _cfg.port);
        if (now - _tcpConnectStartMs > kTcpConnectTimeoutMs) {
            setError_("tcp_connect_timeout");
            return false;
        }
        return false;
    }

    // MqttConnecting / Connected: TCP всё ещё должен быть поднят.
    if (!_net.connected()) {
        setError_("tcp_drop");
        return false;
    }
    return true;
}

void MqttFsmClient::maybeSendPing_(uint32_t now) {
    if (_cfg.keepAliveSec == 0) return;
    const uint32_t kaMs = (uint32_t)_cfg.keepAliveSec * 1000UL;
    // Слот Ctrl может быть свободен, пока Tele в очереди — резерв держит beginQueueSlot_(Ctrl).
    const uint32_t last = (_lastRxMs != 0) ? _lastRxMs : _lastTxMs;
    if (last != 0 && (now - last) < (kaMs / 2)) return;
    if (_lastPingMs != 0 && (now - _lastPingMs) < (kaMs / 2)) return;
    if (buildPingreq_()) {
        _lastPingMs = now;
    }
}

void MqttFsmClient::maybeKeepaliveWatchdog_(uint32_t now) {
    if (_cfg.keepAliveSec == 0 || _lastRxMs == 0) return;
    const uint32_t kaMs = (uint32_t)_cfg.keepAliveSec * 1000UL;
    const uint32_t deadMs =
        (kaMs * (uint32_t)NetTiming::MQTT_KEEPALIVE_DEADMAN_NUM) /
        (uint32_t)NetTiming::MQTT_KEEPALIVE_DEADMAN_DEN;
    if ((now - _lastRxMs) < deadMs) return;
    setError_("keepalive_timeout");
}

bool MqttFsmClient::pastDeadline_(uint32_t deadlineMs) {
    return deadlineMs != 0 && (int32_t)(millis() - deadlineMs) >= 0;
}

void MqttFsmClient::pumpWrite_(uint16_t maxBytes, uint32_t deadlineMs) {
    if (_txQCount == 0) return;
    uint8_t* pkt = _txQ[_txQHead];
    uint16_t pktLen = _txQLen[_txQHead];
    if (pktLen == 0) {
        _txQHead = (uint8_t)((_txQHead + 1u) % TX_Q_DEPTH);
        _txQCount--;
        _txOff = 0;
        return;
    }

    // Держать Tele, пока собирается верный входящий кадр. TX нагрузки CIPSEND
    // без чередующегося опроса RX раньше обрезал +IPD (rx_incomplete); даже
    // при нарезке TX не начинать большую отправку Tele посреди сборки PUBLISH.
    if (_txOff == 0 && _txQClass[_txQHead] == OutClass::Tele) {
        uint32_t need = 0;
        if (mqtt_wire::isValidIncomplete(_rx, _rxLen, (uint16_t)sizeof(_rx), need)) return;
    }

    // Положить весь оставшийся кадр в буфер транспорта, затем один CIPSEND.
    // Частичный CIPSEND пакета MQTT рассинхронизирует поток брокера.
    (void)maxBytes;
    if (pastDeadline_(deadlineMs)) return;

    const uint16_t remaining = (uint16_t)(pktLen - _txOff);
    const size_t w = _net.write(pkt + _txOff, remaining);
    if (w == 0) return;
    _txOff += (uint16_t)w;
    _lastTxMs = millis();

    if (_txOff < pktLen) {
        // Буфер подготовки транспорта полон или занят — ждать; неполный кадр MQTT не сбрасывать.
        return;
    }

    _net.flush();
    const OutClass doneClass = _txQClass[_txQHead];
    const TeleKind doneKind = _txQTeleKind[_txQHead];
    _txQHead = (uint8_t)((_txQHead + 1u) % TX_Q_DEPTH);
    _txQCount--;
    _txOff = 0;
    if (doneClass == OutClass::Tele && _teleDequeuedCb) {
        _teleDequeuedCb(_teleDequeuedCtx, doneKind);
    }
}

void MqttFsmClient::pumpReadAndParse_(uint16_t maxBytes, uint16_t maxFrames, uint32_t deadlineMs) {
    if (maxBytes && !pastDeadline_(deadlineMs)) {
        const int avail = _net.available();
        if (avail > 0) {
            uint16_t room = (_rxLen < sizeof(_rx)) ? (uint16_t)(sizeof(_rx) - _rxLen) : 0;
            uint16_t want = maxBytes;
            if ((uint16_t)avail < want) want = (uint16_t)avail;
            if (room < want) want = room;
            if (want > 0) {
                const int n = _net.read(_rx + _rxLen, want);
                if (n > 0) {
                    _rxLen = (uint16_t)(_rxLen + (uint16_t)n);
                    // Часы простоя: считается только «нет прогресса» — обновлять на каждом байте RX.
                    _rxAssembleStartMs = millis();
                }
            }
        }
    }

    uint16_t frames = 0;
    while (frames < maxFrames) {
        if (pastDeadline_(deadlineMs)) break;
        if (!parseOneFrame_()) break;
        frames++;
    }

    if (_rxLen == 0) {
        _rxAssembleStartMs = 0;
    } else if (_rxAssembleStartMs != 0 &&
               (millis() - _rxAssembleStartMs) >= RX_ASSEMBLE_TIMEOUT_MS) {
        uint32_t need = 0;
        if (mqtt_wire::isValidIncomplete(_rx, _rxLen, (uint16_t)sizeof(_rx), need)) {
            // Жёсткий отказ: не искать по байтам от настоящей головы PUBLISH (иначе «car/...» съедается навсегда).
            logger.log(LogModule::MqttFsmClient, "RX stall, frame incomplete type=%u have=%u need=%u — resyncing session\n",
                       (unsigned)(_rx[0] >> 4), (unsigned)_rxLen, (unsigned)need);
            // Hex-голова для разбора обрезки (до очистки буфера).
            {
                char hex[48];
                const uint16_t n = (_rxLen < 16u) ? _rxLen : 16u;
                size_t hp = 0;
                for (uint16_t i = 0; i < n && hp + 3 < sizeof(hex); ++i) {
                    hp += (size_t)snprintf(hex + hp, sizeof(hex) - hp, "%02X", (unsigned)_rx[i]);
                    if (i + 1u < n && hp + 1 < sizeof(hex)) hex[hp++] = ' ';
                }
                hex[hp < sizeof(hex) ? hp : sizeof(hex) - 1] = '\0';
                logger.log(LogModule::MqttFsmClient, "RX stall hex[%u]=%s\n", (unsigned)n, hex);
            }
            if (_budgets.onRxIncomplete) {
                _budgets.onRxIncomplete(_budgets.onRxIncompleteCtx, _rx, _rxLen, need);
            }
            _rxLen = 0;
            _rxAssembleStartMs = 0;
            setError_("rx_incomplete");
            return;
        }
        // Мусор впереди / рассинхрон: выбросить байт и искать верный тип MQTT.
        logger.log(LogModule::MqttFsmClient, "RX stall rxLen=%u — sync skip 0x%02X\n", (unsigned)_rxLen,
                   (unsigned)_rx[0]);
        memmove(_rx, _rx + 1, (size_t)(_rxLen - 1u));
        _rxLen--;
        _rxAssembleStartMs = _rxLen ? millis() : 0;
    }
}

bool MqttFsmClient::resyncDropOne_(const char* why) {
    if (_rxLen == 0) return false;
    logger.log(LogModule::MqttFsmClient, "RX resync (%s) reset 0x%02X rxLen=%u\n", why ? why : "?", (unsigned)_rx[0],
               (unsigned)_rxLen);
    memmove(_rx, _rx + 1, (size_t)(_rxLen - 1u));
    _rxLen--;
    _rxAssembleStartMs = _rxLen ? millis() : 0;
    return _rxLen > 0;
}

bool MqttFsmClient::parseOneFrame_() {
    if (_rxLen < 2) return false;
    mqtt_wire::FrameHead h;
    switch (mqtt_wire::parseHead(_rx, _rxLen, h)) {
        case mqtt_wire::Head::NeedMore:
            return false;
        case mqtt_wire::Head::BadType:
            // В голове тип не MQTT → поиск по байтам, сеанс не рвать.
            (void)resyncDropOne_("bad_type");
            return false;
        case mqtt_wire::Head::BadRemLen:
            (void)resyncDropOne_("bad_remlen");
            return false;
        case mqtt_wire::Head::Ok:
            break;
    }
    if (h.totalLen > sizeof(_rx)) {
        // От этого головного байта размер кадра невозможен — пропустить и искать дальше.
        (void)resyncDropOne_("oversize");
        return false;
    }
    const uint16_t totalLen = (uint16_t)h.totalLen;
    if (_rxLen < totalLen) return false; // нужно ещё байт

    _lastRxMs = millis();

    const uint8_t pktType = h.type;
    const uint8_t* p = _rx + h.headerLen;

    if (pktType == mqtt_wire::packet::Connack) {
        uint8_t rc = 0;
        if (!mqtt_wire::parseConnack(p, h.remLen, rc)) {
            (void)resyncDropOne_("short_connack");
            return false;
        }
        logger.log(LogModule::MqttFsmClient, "CONNACK code=%u\n", (unsigned)rc);
        if (rc == 0) {
            _state = State::Connected;
            _mqttHandshakeStartMs = 0;
            _awaitingSuback = false;
            _subPacketId = 0;
            _subscribeConfirmed = false;
        } else {
            setError_("connack_refused");
        }
    } else if (pktType == mqtt_wire::packet::Suback) {
        uint16_t pid = 0;
        uint8_t rc = 0;
        if (mqtt_wire::parseSuback(p, h.remLen, pid, rc)) {
            if (_awaitingSuback && pid != _subPacketId) {
                logger.log(LogModule::MqttFsmClient, "SUBACK pid %u != %u — skipped\n", (unsigned)pid,
                           (unsigned)_subPacketId);
            } else {
                _awaitingSuback = false;
                _subPacketId = 0;
                logger.log(LogModule::MqttFsmClient, "SUBACK code=%u\n", (unsigned)rc);
                if (rc == 0x80) {
                    _subscribeConfirmed = false;
                    _subRequested = true;
                } else {
                    _subscribeConfirmed = true;
                }
            }
        }
    } else if (pktType == mqtt_wire::packet::Puback) {
        uint16_t pid = 0;
        if (mqtt_wire::parsePuback(p, h.remLen, pid)) {
            if (_qos1ReplyActive && pid == _qos1ReplyPacketId) {
                clearQos1Reply_();
            }
        }
    } else if (pktType == mqtt_wire::packet::Publish) {
        mqtt_wire::PublishView v;
        if (!mqtt_wire::parsePublish(h.flags, p, h.remLen, v) || v.topicLen >= TOPIC_MAX) {
            // Плохая раскладка PUBLISH на этом смещении — искать дальше, а не переподключаться (ждущие ack сохраняются).
            (void)resyncDropOne_("pub_framing");
            return false;
        }
        char topic[TOPIC_MAX];
        memcpy(topic, p + v.topicOffset, v.topicLen);
        topic[v.topicLen] = '\0';
        bool ackOk = true;
        if (v.qos > 0 && v.packetId != 0) {
            uint8_t* slot = nullptr;
            uint16_t cap = 0;
            ackOk = false;
            if (beginQueueSlot_(OutClass::Ctrl, slot, cap)) {
                const uint16_t n = mqtt_wire::buildPuback(slot, cap, v.packetId);
                if (n) {
                    commitQueueSlot_(n, OutClass::Ctrl);
                    ackOk = true;
                }
            }
        }
        // Не выполнять команду, если PUBACK при QoS>0 не удалось подготовить — брокер доставит снова.
        if (ackOk && _pubCb) {
            _pubCb(_pubCbCtx, topic, p + v.payloadOffset, v.payloadLen, v.retained);
        }
    } else if (pktType == mqtt_wire::packet::Disconnect) {
        setError_("server_disconnect");
    } else {
        // PINGRESP (13) и прочие верные типы: потребить.
    }

    // Обработчик мог уже оборвать сеанс (setError_ чистит буфер RX): потреблять нечего.
    if (_rxLen < totalLen) return false;
    const uint16_t remain = (uint16_t)(_rxLen - totalLen);
    if (remain) memmove(_rx, _rx + totalLen, remain);
    _rxLen = remain;
    if (_rxLen == 0) _rxAssembleStartMs = 0;
    else _rxAssembleStartMs = millis();
    return true;
}

bool MqttFsmClient::buildConnect_() {
    uint8_t* slot = nullptr;
    uint16_t cap = 0;
    if (!beginQueueSlot_(OutClass::Ctrl, slot, cap)) return false;

    mqtt_wire::ConnectOptions o;
    o.mqtt311 = true;
    o.clientId = _cfg.clientId;
    o.username = _cfg.username;
    o.password = _cfg.password;
    o.willTopic = _cfg.willTopic;
    o.willMsg = _cfg.willMsg;
    o.willRetain = _cfg.willRetain;
    o.willQos = _cfg.willQos;
    o.keepAliveSec = _cfg.keepAliveSec;
    o.cleanSession = _cfg.cleanSession;
    if (_cfg.password && _cfg.password[0] && !(_cfg.username && _cfg.username[0])) {
        logger.log(LogModule::MqttFsmClient, "CONNECT: password without username not sent (MQTT 3.1.1)\n");
    }

    const uint16_t n = mqtt_wire::buildConnect(slot, cap, o);
    if (n == 0) {
        logger.log(LogModule::MqttFsmClient, "CONNECT build failed (empty client id or fields too long)\n");
        return false;
    }
    commitQueueSlot_(n, OutClass::Ctrl);
    // CONNECT лежит подготовленным, пока pumpWrite_ не пошлёт байты — lastTx пока не отмечать.
    _lastTxMs = 0;
    return true;
}

bool MqttFsmClient::buildDisconnect_() {
    uint8_t* slot = nullptr;
    uint16_t cap = 0;
    if (!beginQueueSlot_(OutClass::Ctrl, slot, cap)) return false;
    const uint16_t n = mqtt_wire::buildDisconnect(slot, cap);
    if (n == 0) return false;
    commitQueueSlot_(n, OutClass::Ctrl);
    return true;
}

bool MqttFsmClient::buildPingreq_() {
    uint8_t* slot = nullptr;
    uint16_t cap = 0;
    if (!beginQueueSlot_(OutClass::Ctrl, slot, cap)) return false;
    const uint16_t n = mqtt_wire::buildPingreq(slot, cap);
    if (n == 0) return false;
    commitQueueSlot_(n, OutClass::Ctrl);
    return true;
}

bool MqttFsmClient::buildSubscribe_(const char* topic) {
    if (!topic || !topic[0]) return false;
    if (_state != State::Connected) return false;

    uint8_t* slot = nullptr;
    uint16_t cap = 0;
    if (!beginQueueSlot_(OutClass::Ctrl, slot, cap)) return false;
    const uint16_t pid = allocPacketId_();
    const uint16_t n = mqtt_wire::buildSubscribe(slot, cap, pid, topic);
    if (n == 0) return false;
    commitQueueSlot_(n, OutClass::Ctrl);
    _subPacketId = pid;
    return true;
}

bool MqttFsmClient::buildPublish_(const char* topic, const uint8_t* payload, uint16_t len, bool retained,
                                  OutClass cls, TeleKind teleKind, uint8_t qos) {
    if (!topic || !topic[0]) return false;
    if (qos > 1) return false;
    const uint16_t topicLen = (uint16_t)strnlen(topic, TOPIC_MAX);
    if (topicLen == 0 || topicLen >= TOPIC_MAX) return false;

    // Ctrl QoS1: не больше одной копии незавершённого ответа.
    if (cls == OutClass::Ctrl && qos == 1 && _qos1ReplyActive) return false;

    uint8_t* slot = nullptr;
    uint16_t cap = 0;
    uint8_t replaceIdx = 0;
    const bool coalesce =
        (cls == OutClass::Tele) && beginReplaceTeleSlot_(teleKind, slot, cap, replaceIdx);
    if (!coalesce) {
        if (!beginQueueSlot_(cls, slot, cap)) return false;
    }

    uint16_t packetId = 0;
    if (qos == 1) packetId = allocPacketId_();

    const uint16_t n = mqtt_wire::buildPublish(slot, cap, topic, payload, len, retained, qos, packetId, false);
    if (n == 0) {
        logger.log(LogModule::MqttFsmClient, "PUBLISH too large: need=%u txMax=%u (topicLen=%u payloadLen=%u qos=%u)\n",
                   (unsigned)(mqtt_wire::publishHeadLength(topicLen, len, qos) + len), (unsigned)cap,
                   (unsigned)topicLen, (unsigned)len, (unsigned)qos);
        return false;
    }
    if (n > NetTiming::MQTT_CIPSEND_MAX) {
        logger.log(LogModule::MqttFsmClient, "PUBLISH above CIPSEND limit %u > %u\n", (unsigned)n,
                   (unsigned)NetTiming::MQTT_CIPSEND_MAX);
        return false;
    }
    if (coalesce) {
        commitReplaceTeleSlot_(replaceIdx, n, teleKind);
    } else {
        commitQueueSlot_(n, cls, teleKind);
    }

    if (cls == OutClass::Ctrl && qos == 1) {
        if (n <= NetTiming::MQTT_REPLY_QOS1_COPY_MAX) {
            memcpy(_qos1ReplyCopy, slot, n);
            _qos1ReplyLen = n;
            _qos1ReplyPacketId = packetId;
            _qos1ReplyActive = true;
            _qos1ReplyDupSent = false;
            _qos1ReplySentMs = millis();
        } else {
            // Кадр не влезает в копию QoS1 — всё равно в очереди как Ctrl в духе QoS0, без подтверждения.
            logger.log(LogModule::MqttFsmClient, "QoS1 reply copy skipped len=%u\n", (unsigned)n);
        }
    }
    return true;
}

bool MqttFsmClient::publishPrintedMeasured(const char* topic, JsonPrintEncodeFn encoder, void* ctx,
                                          uint16_t maxPayloadBytes, bool retained, bool control,
                                          size_t* measuredBytesOut, TeleKind teleKind, uint8_t qos) {
    if (!topic || !topic[0] || !encoder) return false;
    if (_state != State::Connected) return false;
    if (qos > 1) return false;

    const OutClass cls = control ? OutClass::Ctrl : OutClass::Tele;
    if (cls == OutClass::Ctrl && qos == 1 && _qos1ReplyActive) return false;

    const uint16_t topicLen = (uint16_t)strnlen(topic, TOPIC_MAX);
    if (topicLen == 0 || topicLen >= TOPIC_MAX) return false;

    // Зарезервировать самый длинный заголовок: RL на 4 байта и необязательный packet id для QoS1.
    const uint16_t pidBytes = (qos > 0) ? 2U : 0U;
    const uint16_t hdrMax = (uint16_t)(1U + 4U + 2U + (uint32_t)topicLen + pidBytes);

    uint8_t* slot = nullptr;
    uint16_t cap = 0;
    uint8_t replaceIdx = 0;
    const bool coalesce =
        (cls == OutClass::Tele) && beginReplaceTeleSlot_(teleKind, slot, cap, replaceIdx);
    if (!coalesce) {
        if (!beginQueueSlot_(cls, slot, cap)) return false;
    }
    if (hdrMax >= cap) {
        logger.log(LogModule::MqttFsmClient, "publishJson: header margin %u >= limit %u\n", (unsigned)hdrMax, (unsigned)cap);
        return false;
    }

    // Несхлопываемые Full/Catalog: сначала кодировать тело вне слота, чтобы сбой кодирования не попортил кадр.
    const bool sticky = (teleKind == TeleKind::Full || teleKind == TeleKind::Catalog);
    uint8_t bodyScratch[NetTiming::MQTT_CIPSEND_MAX];
    uint8_t* bodyDst = (coalesce && sticky) ? bodyScratch : (slot + hdrMax);
    const size_t bodyCap = (coalesce && sticky) ? sizeof(bodyScratch) : (size_t)(cap - hdrMax);

    auto failDeltaSlot = [&]() -> bool {
        if (coalesce && teleKind == TeleKind::Delta) {
            commitReplaceTeleSlot_(replaceIdx, mqtt_wire::buildPingreq(slot, cap), TeleKind::Delta);
        }
        return false;
    };

    JsonIntoBufferPrint jp(bodyDst, bodyCap);
    const size_t mj = encoder(jp, ctx);
    if (measuredBytesOut) {
        *measuredBytesOut = mj;
    }
    if (jp.overflowed() || mj == 0 || mj > maxPayloadBytes) {
        logger.log(LogModule::MqttFsmClient, "publishJson: encode=%u maxPayload=%u overflow=%u%s\n", (unsigned)mj,
                   (unsigned)maxPayloadBytes, jp.overflowed() ? 1U : 0U,
                   mj == 0 ? " (empty)" : (mj > maxPayloadBytes ? " (oversize)" : ""));
        return failDeltaSlot();
    }

    uint16_t packetId = 0;
    if (qos == 1) packetId = allocPacketId_();

    const uint16_t hdrExact = mqtt_wire::publishHeadLength(topicLen, (uint32_t)mj, qos);
    const uint32_t total = (uint32_t)hdrExact + (uint32_t)mj;
    if (hdrExact == 0 || total > cap) {
        logger.log(LogModule::MqttFsmClient, "publishJson: packet too large total=%u txMax=%u\n", (unsigned)total,
                   (unsigned)cap);
        return failDeltaSlot();
    }
    if (total > NetTiming::MQTT_CIPSEND_MAX) {
        logger.log(LogModule::MqttFsmClient, "publishJson: above CIPSEND limit %u > %u\n", (unsigned)total,
                   (unsigned)NetTiming::MQTT_CIPSEND_MAX);
        return failDeltaSlot();
    }

    if (coalesce && sticky) {
        if (!mqtt_wire::writePublishHead(slot, cap, topic, topicLen, (uint32_t)mj, retained, qos, packetId, false)) {
            return false; // прежние байты Full/Delta не тронуты
        }
        memcpy(slot + hdrExact, bodyScratch, mj);
        commitReplaceTeleSlot_(replaceIdx, (uint16_t)total, teleKind);
    } else {
        if (hdrExact != hdrMax) {
            memmove(slot + hdrExact, slot + hdrMax, mj);
        }
        if (!mqtt_wire::writePublishHead(slot, cap, topic, topicLen, (uint32_t)mj, retained, qos, packetId, false)) {
            return failDeltaSlot();
        }
        if (coalesce) {
            commitReplaceTeleSlot_(replaceIdx, (uint16_t)total, teleKind);
        } else {
            commitQueueSlot_((uint16_t)total, cls, teleKind);
        }
    }

    if (cls == OutClass::Ctrl && qos == 1) {
        if (total <= NetTiming::MQTT_REPLY_QOS1_COPY_MAX) {
            memcpy(_qos1ReplyCopy, slot, (size_t)total);
            _qos1ReplyLen = (uint16_t)total;
            _qos1ReplyPacketId = packetId;
            _qos1ReplyActive = true;
            _qos1ReplyDupSent = false;
            _qos1ReplySentMs = millis();
        }
    }
    return true;
}
