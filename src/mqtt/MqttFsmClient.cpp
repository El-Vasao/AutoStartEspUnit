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

// Layered with Sim800Tcp::CONNECT_WATCHDOG_MS (15s): transport recovers modem sooner; FSM gives up later.
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
    }
}

int MqttFsmClient::findReplaceableTele_() const {
    for (uint8_t i = 0; i < _txQCount; i++) {
        const uint8_t idx = (uint8_t)((_txQHead + i) % TX_Q_DEPTH);
        if (_txQClass[idx] != OutClass::Tele) continue;
        if (i == 0 && _txOff != 0) continue; // mid-send head
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

bool MqttFsmClient::beginQueueSlot_(OutClass cls, uint8_t*& buf, uint16_t& cap) {
    if (cls == OutClass::Tele) {
        // Lowest priority: never take a new Tele slot while Ctrl is waiting.
        if (hasCtrlInQueue_()) return false;
        // After enqueue must leave ≥1 free slot for Ctrl.
        if (freeSlots_() < 2) return false;
    } else if (!queueHasRoom_()) {
        return false;
    }
    buf = _txQ[_txQTail];
    cap = TX_MAX;
    return true;
}

void MqttFsmClient::commitQueueSlot_(uint16_t len, OutClass cls) {
    if (len == 0 || len > TX_MAX) return;
    _txQLen[_txQTail] = len;
    _txQClass[_txQTail] = cls;
    _txQTail = (uint8_t)((_txQTail + 1u) % TX_Q_DEPTH);
    _txQCount++;
}

bool MqttFsmClient::beginReplaceTeleSlot_(uint8_t*& buf, uint16_t& cap, uint8_t& slotIdxOut) {
    const int idx = findReplaceableTele_();
    if (idx < 0) return false;
    slotIdxOut = (uint8_t)idx;
    buf = _txQ[slotIdxOut];
    cap = TX_MAX;
    return true;
}

void MqttFsmClient::commitReplaceTeleSlot_(uint8_t slotIdx, uint16_t len) {
    if (slotIdx >= TX_Q_DEPTH || len == 0 || len > TX_MAX) return;
    _txQLen[slotIdx] = len;
    _txQClass[slotIdx] = OutClass::Tele;
}

void MqttFsmClient::setError_(const char* reason) {
    _lastErrorReason = reason ? reason : "?";
    logger.log(LogModule::MqttFsmClient, "close tcp: %s\n", _lastErrorReason);
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

bool MqttFsmClient::publish(const char* topic, const uint8_t* payload, uint16_t len, bool retained, bool control) {
    if (!topic || !topic[0]) return false;
    if (!payload && len) return false;
    if (_state != State::Connected) return false;
    return buildPublish_(topic, payload, len, retained, control ? OutClass::Ctrl : OutClass::Tele);
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
        // Drain outbound queue (e.g. retained offline) before DISCONNECT+TCP stop.
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
        // Passive mode: still pump reads to keep buffers drained if something is connected.
        if (_net.connected()) {
            doRead();
            doWrite();
        }
        return;
    }

    // Ensure TCP first.
    if (!ensureTcp_()) return;

    // Once TCP is up, drive MQTT connect then keepalive.
    if (_state == State::TcpConnecting) {
        // Should not happen: ensureTcp_() transitions.
        return;
    }

    if (_state == State::MqttConnecting) {
        // Send CONNECT once.
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

        // Handshake clock may be set with millis() after `now` was snapshotted this tick —
        // guard against uint32 underflow (instant false mqtt_connack_timeout).
        const uint32_t hs0 = (_mqttHandshakeStartMs != 0) ? _mqttHandshakeStartMs : _tcpConnectStartMs;
        const uint32_t nowHs = millis();
        if (_state == State::MqttConnecting && hs0 != 0 && (int32_t)(nowHs - hs0) > (int32_t)kMqttConnectTimeoutMs) {
            setError_("mqtt_connack_timeout");
        }
        return;
    }

    if (_state == State::Connected) {
        // Subscribe request (single topic) — enqueue when a queue slot is free.
        if (_subRequested && !_awaitingSuback && queueHasRoom_()) {
            if (buildSubscribe_(_subTopic)) {
                _awaitingSuback = true;
                _subRequested = false;
            }
        }

        maybeSendPing_(now);
        maybeKeepaliveWatchdog_(now);
        if (_state != State::Connected) return;
        doWrite();
        doRead();
        // Cmd handler may enqueue a reply during doRead — flush this tick.
        doWrite();

        // Transport dropped (CLOSED / SEND FAIL / CIPSHUT) — count as Error for reattach streak.
        // Skip if a frame handler already failed the session (keeps its reason, avoids a second stop()).
        if (_state == State::Connected && !_net.connected()) {
            setError_("tcp_drop");
        }
    }
}

bool MqttFsmClient::ensureTcp_() {
    const uint32_t now = millis();
    // Reconnect after Error: clear sticky Error so Idle→TcpConnecting can start.
    if (_state == State::Error) {
        _state = State::Idle;
    }
    if (_state == State::Idle) {
        // Hold CIPSTART while modem control plane is busy (e.g. CNTP).
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
        // Do not stop() here: transport already closed after setError_/CLOSED.
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
        // Non-blocking connect kick (transport enforces CIPSTART cooldown).
        (void)_net.connect(_cfg.host, _cfg.port);
        if (now - _tcpConnectStartMs > kTcpConnectTimeoutMs) {
            setError_("tcp_connect_timeout");
            return false;
        }
        return false;
    }

    // MqttConnecting / Connected: TCP must still be up.
    if (!_net.connected()) {
        setError_("tcp_drop");
        return false;
    }
    return true;
}

void MqttFsmClient::maybeSendPing_(uint32_t now) {
    if (_cfg.keepAliveSec == 0) return;
    const uint32_t kaMs = (uint32_t)_cfg.keepAliveSec * 1000UL;
    // Ctrl slot may still be free while Tele is queued — beginQueueSlot_(Ctrl) enforces reserve.
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

    // Hold Tele while a valid inbound frame is still assembling. CIPSEND payload
    // TX without interleaved RX poll used to truncate +IPD (rx_incomplete); even
    // with chunked TX, avoid starting a large Tele send mid-PUBLISH assemble.
    if (_txOff == 0 && _txQClass[_txQHead] == OutClass::Tele) {
        uint32_t need = 0;
        if (mqtt_wire::isValidIncomplete(_rx, _rxLen, (uint16_t)sizeof(_rx), need)) return;
    }

    // Stage the entire remaining frame into the transport buffer, then flush one CIPSEND.
    // Partial CIPSEND of an MQTT packet desyncs the broker stream.
    (void)maxBytes;
    if (pastDeadline_(deadlineMs)) return;

    const uint16_t remaining = (uint16_t)(pktLen - _txOff);
    const size_t w = _net.write(pkt + _txOff, remaining);
    if (w == 0) return;
    _txOff += (uint16_t)w;
    _lastTxMs = millis();

    if (_txOff < pktLen) {
        // Transport staging full or busy — wait; do not flush a partial MQTT frame.
        return;
    }

    _net.flush();
    _txQHead = (uint8_t)((_txQHead + 1u) % TX_Q_DEPTH);
    _txQCount--;
    _txOff = 0;
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
                    // Stall clock: only "no progress" counts — refresh on every RX byte.
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
            // Fail-closed: do not byte-hunt from a real PUBLISH head (eats "car/..." forever).
            logger.log(LogModule::MqttFsmClient, "RX stall incomplete type=%u have=%u need=%u — resync session\n",
                       (unsigned)(_rx[0] >> 4), (unsigned)_rxLen, (unsigned)need);
            // Hex head for truncation forensics (before buffer cleared).
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
        // Leading junk / desync: drop one byte and hunt for a valid MQTT type.
        logger.log(LogModule::MqttFsmClient, "RX stall rxLen=%u — sync skip 0x%02X\n", (unsigned)_rxLen,
                   (unsigned)_rx[0]);
        memmove(_rx, _rx + 1, (size_t)(_rxLen - 1u));
        _rxLen--;
        _rxAssembleStartMs = _rxLen ? millis() : 0;
    }
}

bool MqttFsmClient::resyncDropOne_(const char* why) {
    if (_rxLen == 0) return false;
    logger.log(LogModule::MqttFsmClient, "RX resync (%s) drop 0x%02X rxLen=%u\n", why ? why : "?", (unsigned)_rx[0],
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
            // Non-MQTT type at head → byte-hunt, do not tear down the session.
            (void)resyncDropOne_("bad_type");
            return false;
        case mqtt_wire::Head::BadRemLen:
            (void)resyncDropOne_("bad_remlen");
            return false;
        case mqtt_wire::Head::Ok:
            break;
    }
    if (h.totalLen > sizeof(_rx)) {
        // Impossible frame size from this head byte — skip and hunt.
        (void)resyncDropOne_("oversize");
        return false;
    }
    const uint16_t totalLen = (uint16_t)h.totalLen;
    if (_rxLen < totalLen) return false; // need more bytes

    _lastRxMs = millis();

    const uint8_t pktType = h.type;
    const uint8_t* p = _rx + h.headerLen;

    if (pktType == mqtt_wire::packet::Connack) {
        uint8_t rc = 0;
        if (!mqtt_wire::parseConnack(p, h.remLen, rc)) {
            (void)resyncDropOne_("short_connack");
            return false;
        }
        logger.log(LogModule::MqttFsmClient, "CONNACK rc=%u\n", (unsigned)rc);
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
                logger.log(LogModule::MqttFsmClient, "SUBACK pid %u != %u — ignore\n", (unsigned)pid,
                           (unsigned)_subPacketId);
            } else {
                _awaitingSuback = false;
                _subPacketId = 0;
                logger.log(LogModule::MqttFsmClient, "SUBACK rc=%u\n", (unsigned)rc);
                if (rc == 0x80) {
                    _subscribeConfirmed = false;
                    _subRequested = true;
                } else {
                    _subscribeConfirmed = true;
                }
            }
        }
    } else if (pktType == mqtt_wire::packet::Publish) {
        mqtt_wire::PublishView v;
        if (!mqtt_wire::parsePublish(h.flags, p, h.remLen, v) || v.topicLen >= TOPIC_MAX) {
            // Bad PUBLISH layout at this offset — hunt rather than reconnect (preserves pending acks).
            (void)resyncDropOne_("pub_framing");
            return false;
        }
        char topic[TOPIC_MAX];
        memcpy(topic, p + v.topicOffset, v.topicLen);
        topic[v.topicLen] = '\0';
        if (v.qos > 0 && v.packetId != 0) {
            uint8_t* slot = nullptr;
            uint16_t cap = 0;
            if (beginQueueSlot_(OutClass::Ctrl, slot, cap)) {
                const uint16_t n = mqtt_wire::buildPuback(slot, cap, v.packetId);
                if (n) commitQueueSlot_(n, OutClass::Ctrl);
            }
        }
        if (_pubCb) {
            _pubCb(_pubCbCtx, topic, p + v.payloadOffset, v.payloadLen, v.retained);
        }
    } else if (pktType == mqtt_wire::packet::Disconnect) {
        setError_("server_disconnect");
    } else {
        // PINGRESP (13) and other valid types: consume.
    }

    // A handler may have torn the session down (setError_ clears the RX buffer): nothing left to consume.
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
    o.mqtt311 = (_cfg.proto != Proto::Mqtt31);
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
        logger.log(LogModule::MqttFsmClient, "CONNECT: password without username is not sent (MQTT 3.1.1)\n");
    }

    const uint16_t n = mqtt_wire::buildConnect(slot, cap, o);
    if (n == 0) {
        logger.log(LogModule::MqttFsmClient, "CONNECT build failed (client id empty or fields too long)\n");
        return false;
    }
    commitQueueSlot_(n, OutClass::Ctrl);
    // CONNECT staged until pumpWrite_ sends bytes — do not mark lastTx yet.
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
    // Packet id is consumed only when SUBSCRIBE is actually queued; 0 is not a valid id.
    const uint16_t pid = _nextPacketId ? _nextPacketId : 1;
    const uint16_t n = mqtt_wire::buildSubscribe(slot, cap, pid, topic);
    if (n == 0) return false;
    commitQueueSlot_(n, OutClass::Ctrl);
    _subPacketId = pid;
    _nextPacketId = (uint16_t)(pid == 0xFFFF ? 1 : pid + 1);
    return true;
}

bool MqttFsmClient::buildPublish_(const char* topic, const uint8_t* payload, uint16_t len, bool retained,
                                  OutClass cls) {
    if (!topic || !topic[0]) return false;
    const uint16_t topicLen = (uint16_t)strnlen(topic, TOPIC_MAX);
    if (topicLen == 0 || topicLen >= TOPIC_MAX) return false;

    uint8_t* slot = nullptr;
    uint16_t cap = 0;
    uint8_t replaceIdx = 0;
    const bool coalesce = (cls == OutClass::Tele) && beginReplaceTeleSlot_(slot, cap, replaceIdx);
    if (!coalesce) {
        if (!beginQueueSlot_(cls, slot, cap)) return false;
    }
    // buildPublish leaves the slot untouched on failure, so a coalesced Tele packet stays valid.
    const uint16_t n = mqtt_wire::buildPublish(slot, cap, topic, payload, len, retained);
    if (n == 0) {
        logger.log(LogModule::MqttFsmClient, "PUBLISH too large: need=%u txMax=%u (topicLen=%u payloadLen=%u)\n",
                   (unsigned)(mqtt_wire::publishHeadLength(topicLen, len) + len), (unsigned)cap,
                   (unsigned)topicLen, (unsigned)len);
        return false;
    }
    if (coalesce) {
        commitReplaceTeleSlot_(replaceIdx, n);
    } else {
        commitQueueSlot_(n, cls);
    }
    // lastTx only in pumpWrite_ after real UART bytes leave.
    return true;
}

bool MqttFsmClient::publishPrintedMeasured(const char* topic, JsonPrintEncodeFn encoder, void* ctx,
                                          uint16_t maxPayloadBytes, bool retained, bool control,
                                          size_t* measuredBytesOut) {
    if (!topic || !topic[0] || !encoder) return false;
    if (_state != State::Connected) return false;

    const OutClass cls = control ? OutClass::Ctrl : OutClass::Tele;
    const uint16_t topicLen = (uint16_t)strnlen(topic, TOPIC_MAX);
    if (topicLen == 0 || topicLen >= TOPIC_MAX) return false;

    // Single encode pass: reserve the longest header (4-byte Remaining Length), encode payload, then pack.
    const uint16_t hdrMax = (uint16_t)(1U + 4U + 2U + (uint32_t)topicLen);

    uint8_t* slot = nullptr;
    uint16_t cap = 0;
    uint8_t replaceIdx = 0;
    const bool coalesce = (cls == OutClass::Tele) && beginReplaceTeleSlot_(slot, cap, replaceIdx);
    if (!coalesce) {
        if (!beginQueueSlot_(cls, slot, cap)) return false;
    }
    if (hdrMax >= cap) {
        logger.log(LogModule::MqttFsmClient, "publishJson: header reserve %u >= cap %u\n", (unsigned)hdrMax, (unsigned)cap);
        return false;
    }

    // The encoder writes into the slot directly. When coalescing, a failure has already overwritten
    // the queued Tele packet: replace it with a harmless PINGREQ instead of leaving garbage on the wire.
    auto failSlot = [&]() {
        if (coalesce) commitReplaceTeleSlot_(replaceIdx, mqtt_wire::buildPingreq(slot, cap));
        return false;
    };

    JsonIntoBufferPrint jp(slot + hdrMax, (size_t)(cap - hdrMax));
    const size_t mj = encoder(jp, ctx);
    if (measuredBytesOut) {
        *measuredBytesOut = mj;
    }
    if (jp.overflowed() || mj == 0 || mj > maxPayloadBytes) {
        logger.log(LogModule::MqttFsmClient, "publishJson: encode=%u maxPayload=%u overflow=%u%s\n", (unsigned)mj,
                   (unsigned)maxPayloadBytes, jp.overflowed() ? 1U : 0U,
                   mj == 0 ? " (empty)" : (mj > maxPayloadBytes ? " (oversize)" : ""));
        return failSlot();
    }

    const uint16_t hdrExact = mqtt_wire::publishHeadLength(topicLen, (uint32_t)mj);
    const uint32_t total = (uint32_t)hdrExact + (uint32_t)mj;
    if (hdrExact == 0 || total > cap) {
        logger.log(LogModule::MqttFsmClient, "publishJson: packet too large total=%u txMax=%u\n", (unsigned)total,
                   (unsigned)cap);
        return failSlot();
    }

    // Slide payload left if Remaining Length used fewer than 4 bytes.
    if (hdrExact != hdrMax) {
        memmove(slot + hdrExact, slot + hdrMax, mj);
    }
    mqtt_wire::writePublishHead(slot, cap, topic, topicLen, (uint32_t)mj, retained);

    if (coalesce) {
        commitReplaceTeleSlot_(replaceIdx, (uint16_t)total);
    } else {
        commitQueueSlot_((uint16_t)total, cls);
    }
    return true;
}
