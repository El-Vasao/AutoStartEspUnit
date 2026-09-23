#include "mqtt/MqttFsmClient.h"

#include "json/JsonCountingPrint.h"

#include "common/Constants.h"
#include <string.h>
#include "common/Logger.h"

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
static constexpr uint32_t kTcpConnectTimeoutMs = 45000;
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
    logger.log("[MqttFsm] close tcp: %s\n", _lastErrorReason);
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

        // MQTT handshake timeout is measured from first entry into MqttConnecting (or from CONNECT enqueue).
        const uint32_t hs0 = (_mqttHandshakeStartMs != 0) ? _mqttHandshakeStartMs : _tcpConnectStartMs;
        if (hs0 != 0 && (now - hs0) > kMqttConnectTimeoutMs) {
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
        if (!_net.connected()) {
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
        if (stallIsValidIncomplete_(need)) {
            // Fail-closed: do not byte-hunt from a real PUBLISH head (eats "car/..." forever).
            logger.log("[MqttFsm] RX stall incomplete type=%u have=%u need=%u — resync session\n",
                       (unsigned)(_rx[0] >> 4), (unsigned)_rxLen, (unsigned)need);
            _rxLen = 0;
            _rxAssembleStartMs = 0;
            setError_("rx_incomplete");
            return;
        }
        // Leading junk / desync: drop one byte and hunt for a valid MQTT type.
        logger.log("[MqttFsm] RX stall rxLen=%u — sync skip 0x%02X\n", (unsigned)_rxLen,
                   (unsigned)_rx[0]);
        memmove(_rx, _rx + 1, (size_t)(_rxLen - 1u));
        _rxLen--;
        _rxAssembleStartMs = _rxLen ? millis() : 0;
    }
}

bool MqttFsmClient::stallIsValidIncomplete_(uint32_t& needOut) const {
    needOut = 0;
    if (_rxLen < 2) return false;
    const uint8_t pktType = (uint8_t)(_rx[0] >> 4);
    if (pktType < 1 || pktType > 14) return false;
    uint32_t remLen = 0;
    uint8_t used = 0;
    if (!decodeRemainingLen_(_rx + 1, (uint16_t)(_rxLen - 1), remLen, used)) return false;
    const uint32_t totalLen32 = (uint32_t)(1u + used) + remLen;
    if (totalLen32 > sizeof(_rx) || (uint32_t)_rxLen >= totalLen32) return false;
    needOut = totalLen32;
    return true;
}

bool MqttFsmClient::resyncDropOne_(const char* why) {
    if (_rxLen == 0) return false;
    logger.log("[MqttFsm] RX resync (%s) drop 0x%02X rxLen=%u\n", why ? why : "?", (unsigned)_rx[0],
               (unsigned)_rxLen);
    memmove(_rx, _rx + 1, (size_t)(_rxLen - 1u));
    _rxLen--;
    _rxAssembleStartMs = _rxLen ? millis() : 0;
    return _rxLen > 0;
}

bool MqttFsmClient::parseOneFrame_() {
    if (_rxLen < 2) return false;
    const uint8_t typeFlags = _rx[0];
    const uint8_t pktTypeEarly = (uint8_t)(typeFlags >> 4);
    // Non-MQTT type at head → byte-hunt, do not tear down the session.
    if (pktTypeEarly < 1 || pktTypeEarly > 14) {
        (void)resyncDropOne_("bad_type");
        return false;
    }

    uint32_t remLen = 0;
    uint8_t used = 0;
    if (!decodeRemainingLen_(_rx + 1, (uint16_t)(_rxLen - 1), remLen, used)) {
        // 4 continuation bytes already present ⇒ malformed head, resync.
        if ((uint16_t)(_rxLen - 1) >= 4) {
            (void)resyncDropOne_("bad_remlen");
        }
        return false;
    }

    const uint16_t headerLen = (uint16_t)(1 + used);
    const uint32_t totalLen32 = (uint32_t)headerLen + remLen;
    if (totalLen32 > sizeof(_rx)) {
        // Impossible frame size from this head byte — skip and hunt.
        (void)resyncDropOne_("oversize");
        return false;
    }
    const uint16_t totalLen = (uint16_t)totalLen32;
    if (_rxLen < totalLen) return false; // need more bytes


    _lastRxMs = millis();

    const uint8_t pktType = pktTypeEarly;
    const uint8_t flags = (uint8_t)(typeFlags & 0x0F);
    const uint8_t* p = _rx + headerLen;

    if (pktType == 2 /* CONNACK */) {
        if (remLen >= 2) {
            const uint8_t rc = p[1];
            logger.log("[MqttFsm] CONNACK rc=%u\n", (unsigned)rc);
            if (rc == 0) {
                _state = State::Connected;
                _mqttHandshakeStartMs = 0;
                _awaitingSuback = false;
                _subPacketId = 0;
                _subscribeConfirmed = false;
            } else {
                setError_("connack_refused");
            }
        }
    } else if (pktType == 9 /* SUBACK */) {
        if (remLen >= 3) {
            const uint16_t pid = (uint16_t)((p[0] << 8) | p[1]);
            const uint8_t rc = p[2];
            (void)pid;
            _awaitingSuback = false;
            _subPacketId = 0;
            logger.log("[MqttFsm] SUBACK rc=%u\n", (unsigned)rc);
            if (rc == 0x80) {
                _subscribeConfirmed = false;
                _subRequested = true;
            } else {
                _subscribeConfirmed = true;
            }
        }
    } else if (pktType == 3 /* PUBLISH */) {
        const bool retained = (flags & 0x01) != 0;
        if (remLen < 2) {
            (void)resyncDropOne_("pub_short");
            return false;
        }
        const uint16_t topicLen = (uint16_t)((p[0] << 8) | p[1]);
        if ((uint32_t)(topicLen + 2U) > remLen || topicLen == 0 || topicLen >= TOPIC_MAX) {
            // Bad PUBLISH layout at this offset — hunt rather than reconnect (preserves pending acks).
            (void)resyncDropOne_("pub_framing");
            return false;
        }
        char topic[TOPIC_MAX];
        memcpy(topic, p + 2, topicLen);
        topic[topicLen] = '\0';
        uint16_t off = (uint16_t)(2 + topicLen);
        if ((flags & 0x06) != 0) {
            if ((uint32_t)(off + 2U) <= remLen) off += 2;
        }
        const uint16_t payLen = (off <= remLen) ? (uint16_t)(remLen - off) : 0;
        if (_pubCb) {
            _pubCb(_pubCbCtx, topic, p + off, payLen, retained);
        }
    } else if (pktType == 14 /* DISCONNECT */) {
        setError_("server_disconnect");
    } else {
        // PINGRESP (13) and other valid types: consume.
    }

    const uint16_t remain = (uint16_t)(_rxLen - totalLen);
    if (remain) memmove(_rx, _rx + totalLen, remain);
    _rxLen = remain;
    if (_rxLen == 0) _rxAssembleStartMs = 0;
    else _rxAssembleStartMs = millis();
    return true;
}

uint16_t MqttFsmClient::putStr_(uint8_t* p, uint16_t cap, const char* s) {
    if (!s) s = "";
    const uint16_t len = (uint16_t)strnlen(s, cap >= 2 ? (cap - 2) : 0);
    if (cap < (uint16_t)(2 + len)) return 0;
    p[0] = (uint8_t)(len >> 8);
    p[1] = (uint8_t)(len & 0xFF);
    memcpy(p + 2, s, len);
    return (uint16_t)(2 + len);
}

uint8_t MqttFsmClient::encodeRemainingLen_(uint32_t len, uint8_t out[4]) {
    uint8_t i = 0;
    do {
        uint8_t d = (uint8_t)(len % 128);
        len /= 128;
        if (len > 0) d |= 0x80;
        out[i++] = d;
    } while (len > 0 && i < 4);
    return i;
}

bool MqttFsmClient::decodeRemainingLen_(const uint8_t* p, uint16_t cap, uint32_t& outLen, uint8_t& outUsed) {
    uint32_t multiplier = 1;
    uint32_t value = 0;
    uint8_t i = 0;
    while (i < 4) {
        if (i >= cap) return false;
        const uint8_t d = p[i];
        value += (uint32_t)(d & 127) * multiplier;
        multiplier *= 128;
        i++;
        if ((d & 128) == 0) {
            outLen = value;
            outUsed = i;
            return true;
        }
    }
    return false;
}

bool MqttFsmClient::buildConnect_() {
    if (!queueHasRoom_()) return false;
    if (!_cfg.clientId || !_cfg.clientId[0]) return false;

    // Variable header
    // Protocol:
    // - MQTT 3.1:  name="MQIsdp", level=3
    // - MQTT 3.1.1 name="MQTT",   level=4
    uint8_t vh[16];
    uint16_t vhLen = 0;
    const char* protoName = (_cfg.proto == Proto::Mqtt31) ? "MQIsdp" : "MQTT";
    const uint8_t protoLevel = (_cfg.proto == Proto::Mqtt31) ? 0x03 : 0x04;
    vhLen += putStr_(vh + vhLen, (uint16_t)(sizeof(vh) - vhLen), protoName);
    if (vhLen == 0) return false;
    vh[vhLen++] = protoLevel;

    uint8_t connectFlags = 0;
    if (_cfg.cleanSession) connectFlags |= 0x02;
    const bool hasWill = _cfg.willTopic && _cfg.willTopic[0] && _cfg.willMsg;
    if (hasWill) {
        connectFlags |= 0x04; // Will flag (MQTT 3.1.1)
        connectFlags |= (uint8_t)((_cfg.willQos & 3u) << 3); // Will QoS in bits 4..3
        if (_cfg.willRetain) connectFlags |= 0x20;
    }
    const bool hasUser = _cfg.username && _cfg.username[0];
    const bool hasPass = _cfg.password && _cfg.password[0];
    if (hasUser) connectFlags |= 0x80;
    if (hasPass) connectFlags |= 0x40;
    vh[vhLen++] = connectFlags;
    vh[vhLen++] = (uint8_t)(_cfg.keepAliveSec >> 8);
    vh[vhLen++] = (uint8_t)(_cfg.keepAliveSec & 0xFF);

    // Payload: client id, will topic/msg, username/password (in this order)
    uint8_t pl[200];
    uint16_t plLen = 0;
    uint16_t n = putStr_(pl + plLen, (uint16_t)(sizeof(pl) - plLen), _cfg.clientId);
    if (!n) return false;
    plLen += n;
    if (hasWill) {
        n = putStr_(pl + plLen, (uint16_t)(sizeof(pl) - plLen), _cfg.willTopic);
        if (!n) return false;
        plLen += n;
        n = putStr_(pl + plLen, (uint16_t)(sizeof(pl) - plLen), _cfg.willMsg);
        if (!n) return false;
        plLen += n;
    }
    if (hasUser) {
        n = putStr_(pl + plLen, (uint16_t)(sizeof(pl) - plLen), _cfg.username);
        if (!n) return false;
        plLen += n;
    }
    if (hasPass) {
        n = putStr_(pl + plLen, (uint16_t)(sizeof(pl) - plLen), _cfg.password);
        if (!n) return false;
        plLen += n;
    }

    const uint32_t remLen = (uint32_t)vhLen + (uint32_t)plLen;
    uint8_t rl[4];
    const uint8_t rlLen = encodeRemainingLen_(remLen, rl);

    const uint16_t total = (uint16_t)(1 + rlLen + vhLen + plLen);
    uint8_t* slot = nullptr;
    uint16_t cap = 0;
    if (!beginQueueSlot_(OutClass::Ctrl, slot, cap) || total > cap) return false;

    uint16_t off = 0;
    slot[off++] = 0x10; // CONNECT
    memcpy(slot + off, rl, rlLen);
    off += rlLen;
    memcpy(slot + off, vh, vhLen);
    off += vhLen;
    memcpy(slot + off, pl, plLen);
    off += plLen;

    commitQueueSlot_(off, OutClass::Ctrl);
    // CONNECT staged until pumpWrite_ sends bytes — do not mark lastTx yet.
    _lastTxMs = 0;
    return true;
}

bool MqttFsmClient::buildDisconnect_() {
    uint8_t* slot = nullptr;
    uint16_t cap = 0;
    if (!beginQueueSlot_(OutClass::Ctrl, slot, cap) || cap < 2) return false;
    slot[0] = 0xE0;
    slot[1] = 0x00;
    commitQueueSlot_(2, OutClass::Ctrl);
    return true;
}

bool MqttFsmClient::buildPingreq_() {
    uint8_t* slot = nullptr;
    uint16_t cap = 0;
    if (!beginQueueSlot_(OutClass::Ctrl, slot, cap) || cap < 2) return false;
    slot[0] = 0xC0;
    slot[1] = 0x00;
    commitQueueSlot_(2, OutClass::Ctrl);
    return true;
}

bool MqttFsmClient::buildSubscribe_(const char* topic) {
    if (!topic || !topic[0]) return false;
    if (_state != State::Connected) return false;

    const uint16_t pid = _nextPacketId++;
    _subPacketId = pid;

    uint8_t pl[128];
    uint16_t plLen = 0;
    uint16_t n = putStr_(pl + plLen, (uint16_t)(sizeof(pl) - plLen), topic);
    if (!n) return false;
    plLen += n;
    if ((uint16_t)(plLen + 1U) > (uint16_t)sizeof(pl)) return false;
    pl[plLen++] = 0x00; // QoS0

    const uint32_t remLen = 2 + plLen;
    uint8_t rl[4];
    const uint8_t rlLen = encodeRemainingLen_(remLen, rl);
    const uint16_t total = (uint16_t)(1 + rlLen + 2 + plLen);

    uint8_t* slot = nullptr;
    uint16_t cap = 0;
    if (!beginQueueSlot_(OutClass::Ctrl, slot, cap) || total > cap) return false;

    uint16_t off = 0;
    slot[off++] = 0x82; // SUBSCRIBE (type=8, flags=0010)
    memcpy(slot + off, rl, rlLen);
    off += rlLen;
    off += writeU16_(slot + off, pid);
    memcpy(slot + off, pl, plLen);
    off += plLen;

    commitQueueSlot_(off, OutClass::Ctrl);
    return true;
}

bool MqttFsmClient::buildPublish_(const char* topic, const uint8_t* payload, uint16_t len, bool retained,
                                  OutClass cls) {
    if (!topic || !topic[0]) return false;
    const uint16_t topicLen = (uint16_t)strnlen(topic, 255);
    if (topicLen == 0 || topicLen > 255) return false;

    const uint32_t remLen = 2 + topicLen + len;
    uint8_t rl[4];
    const uint8_t rlLen = encodeRemainingLen_(remLen, rl);
    const uint16_t total = (uint16_t)(1 + rlLen + 2 + topicLen + len);

    uint8_t* slot = nullptr;
    uint16_t cap = 0;
    uint8_t replaceIdx = 0;
    const bool coalesce = (cls == OutClass::Tele) && beginReplaceTeleSlot_(slot, cap, replaceIdx);
    if (!coalesce) {
        if (!beginQueueSlot_(cls, slot, cap)) return false;
    }
    if (total > cap) {
        logger.log("[MqttFsm] PUBLISH too large: need=%u txMax=%u (topicLen=%u payloadLen=%u)\n",
                   (unsigned)total, (unsigned)cap, (unsigned)topicLen, (unsigned)len);
        return false;
    }

    uint16_t off = 0;
    uint8_t hdr = 0x30; // PUBLISH QoS0
    if (retained) hdr |= 0x01;
    slot[off++] = hdr;
    memcpy(slot + off, rl, rlLen);
    off += rlLen;
    slot[off++] = (uint8_t)(topicLen >> 8);
    slot[off++] = (uint8_t)(topicLen & 0xFF);
    memcpy(slot + off, topic, topicLen);
    off += topicLen;
    if (len) {
        memcpy(slot + off, payload, len);
        off += len;
    }
    if (coalesce) {
        commitReplaceTeleSlot_(replaceIdx, off);
    } else {
        commitQueueSlot_(off, cls);
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

    JsonCountingPrint measure;
    const size_t mj = encoder(measure, ctx);
    if (measuredBytesOut) {
        *measuredBytesOut = mj;
    }
    if (mj == 0 || mj > maxPayloadBytes) {
        logger.log("[MqttFsm] publishJson: measurePrint=%u maxPayload=%u%s\n", (unsigned)mj,
                   (unsigned)maxPayloadBytes, mj == 0 ? " (empty encode)" : " (oversize)");
        return false;
    }

    const uint16_t topicLen = (uint16_t)strnlen(topic, 255);
    if (topicLen == 0 || topicLen > 255) return false;

    const uint32_t remLen = 2U + (uint32_t)topicLen + (uint32_t)mj;
    uint8_t rl[4];
    const uint8_t rlLen = encodeRemainingLen_(remLen, rl);
    const uint16_t total = (uint16_t)(1U + (uint32_t)rlLen + 2U + (uint32_t)topicLen + (uint32_t)mj);

    uint8_t* slot = nullptr;
    uint16_t cap = 0;
    uint8_t replaceIdx = 0;
    const bool coalesce = (cls == OutClass::Tele) && beginReplaceTeleSlot_(slot, cap, replaceIdx);
    if (!coalesce) {
        if (!beginQueueSlot_(cls, slot, cap)) return false;
    }
    if (total > cap) {
        logger.log("[MqttFsm] publishJson: packet too large total=%u txMax=%u topicLen=%u json=%u\n",
                   (unsigned)total, (unsigned)TX_MAX, (unsigned)topicLen, (unsigned)mj);
        return false;
    }

    uint16_t off = 0;
    uint8_t hdr = 0x30;
    if (retained) hdr |= 0x01;
    slot[off++] = hdr;
    memcpy(slot + off, rl, rlLen);
    off += rlLen;
    slot[off++] = (uint8_t)(topicLen >> 8);
    slot[off++] = (uint8_t)(topicLen & 0xFF);
    memcpy(slot + off, topic, topicLen);
    off += topicLen;

    JsonIntoBufferPrint jp(slot + off, (size_t)(cap - off));
    const size_t w = encoder(jp, ctx);
    if (jp.overflowed() || w != mj) {
        logger.log("[MqttFsm] publishJson: encode mismatch wrote=%u expected=%u overflow=%u\n", (unsigned)w,
                   (unsigned)mj, jp.overflowed() ? 1U : 0U);
        return false;
    }
    off += (uint16_t)w;

    if (coalesce) {
        commitReplaceTeleSlot_(replaceIdx, off);
    } else {
        commitQueueSlot_(off, cls);
    }
    // lastTx only in pumpWrite_ after real UART bytes leave.
    return true;
}

