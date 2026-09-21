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
    _txLen = 0;
    _txOff = 0;
    _rxLen = 0;
    _tcpConnectStartMs = 0;
    _mqttHandshakeStartMs = 0;
    _lastRxMs = 0;
    _lastTxMs = 0;
    _lastPingMs = 0;
    _subRequested = false;
    _awaitingSuback = false;
    _subPacketId = 0;
    _subTopic[0] = '\0';
}

void MqttFsmClient::setError_(const char* reason) {
    logger.log("[MqttFsm] close tcp: %s\n", reason ? reason : "?");
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

bool MqttFsmClient::publish(const char* topic, const uint8_t* payload, uint16_t len, bool retained) {
    if (!topic || !topic[0]) return false;
    if (!payload && len) return false;
    if (_state != State::Connected) return false;
    if (_txLen != 0) return false; // single outstanding packet for simplicity
    if (!buildPublish_(topic, payload, len, retained)) return false;
    return true;
}

void MqttFsmClient::tick(const Budgets& b) {
    _budgets = b;
    const uint32_t now = millis();

    if (_disconnectRequested) {
        if (_state == State::Connected && _txLen == 0) {
            (void)buildDisconnect_();
        }
        pumpWrite_(b.maxWriteBytesPerTick);
        _net.stop();
        resetSession_();
        _state = State::Idle;
        _disconnectRequested = false;
        return;
    }

    if (!_connectRequested) {
        // Passive mode: still pump reads to keep buffers drained if something is connected.
        if (_net.connected()) {
            pumpReadAndParse_(b.maxReadBytesPerTick, b.maxParseFramesPerTick);
            pumpWrite_(b.maxWriteBytesPerTick);
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
        if (_txLen == 0 && _lastTxMs == 0) {
            if (!buildConnect_()) {
                setError_("connect_build");
                return;
            }
            if (_mqttHandshakeStartMs == 0) {
                _mqttHandshakeStartMs = millis();
            }
        }
        pumpWrite_(b.maxWriteBytesPerTick);
        pumpReadAndParse_(b.maxReadBytesPerTick, b.maxParseFramesPerTick);

        // MQTT handshake timeout is measured from first entry into MqttConnecting (or from CONNECT enqueue).
        const uint32_t hs0 = (_mqttHandshakeStartMs != 0) ? _mqttHandshakeStartMs : _tcpConnectStartMs;
        if (hs0 != 0 && (now - hs0) > kMqttConnectTimeoutMs) {
            setError_("mqtt_connack_timeout");
        }
        return;
    }

    if (_state == State::Connected) {
        // Subscribe request (single topic)
        if (_subRequested && !_awaitingSuback && _txLen == 0) {
            if (buildSubscribe_(_subTopic)) {
                _awaitingSuback = true;
                _subRequested = false;
            }
        }

        maybeSendPing_(now);
        pumpWrite_(b.maxWriteBytesPerTick);
        pumpReadAndParse_(b.maxReadBytesPerTick, b.maxParseFramesPerTick);

        // If underlying transport dropped, go back to connect.
        if (!_net.connected()) {
            resetSession_();
            _state = State::Idle;
        }
    }
}

bool MqttFsmClient::ensureTcp_() {
    const uint32_t now = millis();
    if (_state == State::Idle) {
        // If TCP is already up (some transports report connected immediately after URC),
        // do NOT force-stop it here: that would generate CIPCLOSE right after SEND OK.
        if (_net.connected()) {
            _tcpConnectStartMs = now;
            _mqttHandshakeStartMs = 0;
            _state = State::MqttConnecting;
            return true;
        }
        // Socket already down: do not call stop() here — Sim800TcpTransport::stop() would only log noise
        // (wasConnectedOrConnecting is false) after setError_/CLOSED and confused operators with double "stop()".
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
        // Non-blocking connect kick (transport enforces CIPSTART cooldown / busy gates).
        (void)_net.connect(_cfg.host, _cfg.port);
        if (now - _tcpConnectStartMs > kTcpConnectTimeoutMs) {
            setError_("tcp_connect_timeout");
            return false;
        }
        return false;
    }

    // Already TCP connected or beyond.
    if (!_net.connected()) {
        _state = State::Idle;
        return false;
    }
    return true;
}

void MqttFsmClient::maybeSendPing_(uint32_t now) {
    if (_cfg.keepAliveSec == 0) return;
    const uint32_t kaMs = (uint32_t)_cfg.keepAliveSec * 1000UL;
    if (_txLen != 0) return;
    // If we received anything recently, no need to ping.
    const uint32_t last = (_lastRxMs != 0) ? _lastRxMs : _lastTxMs;
    if (last != 0 && (now - last) < (kaMs / 2)) return;
    if (_lastPingMs != 0 && (now - _lastPingMs) < (kaMs / 2)) return;
    if (buildPingreq_()) {
        _lastPingMs = now;
    }
}

void MqttFsmClient::pumpWrite_(uint16_t maxBytes) {
    if (_txLen == 0) return;
    uint16_t remaining = (uint16_t)(_txLen - _txOff);
    if (remaining == 0) {
        _txLen = 0;
        _txOff = 0;
        return;
    }
    uint16_t budget = maxBytes;
    while (budget && remaining) {
        const uint16_t chunk = (remaining < budget) ? remaining : budget;
        const size_t w = _net.write(_tx + _txOff, chunk);
        if (w == 0) break;
        _txOff += (uint16_t)w;
        budget -= (uint16_t)w;
        remaining = (uint16_t)(_txLen - _txOff);
        _lastTxMs = millis();
    }
    if (_txOff >= _txLen) {
        _txLen = 0;
        _txOff = 0;
    }
}

void MqttFsmClient::pumpReadAndParse_(uint16_t maxBytes, uint16_t maxFrames) {
    uint16_t budget = maxBytes;
    while (budget) {
        const int a = _net.available();
        if (a <= 0) break;
        const int c = _net.read();
        if (c < 0) break;
        if (_rxLen < sizeof(_rx)) {
            _rx[_rxLen++] = (uint8_t)c;
        }
        budget--;
    }

    uint16_t frames = 0;
    while (frames < maxFrames) {
        if (!parseOneFrame_()) break;
        frames++;
    }
}

bool MqttFsmClient::parseOneFrame_() {
    if (_rxLen < 2) return false;
    const uint8_t typeFlags = _rx[0];

    uint32_t remLen = 0;
    uint8_t used = 0;
    if (!decodeRemainingLen_(_rx + 1, (uint16_t)(_rxLen - 1), remLen, used)) {
        return false; // need more bytes
    }

    const uint16_t headerLen = (uint16_t)(1 + used);
    const uint32_t totalLen32 = (uint32_t)headerLen + remLen;
    if (totalLen32 > sizeof(_rx)) {
        // Frame too large for our buffer -> drop everything.
        _rxLen = 0;
        return false;
    }
    const uint16_t totalLen = (uint16_t)totalLen32;
    if (_rxLen < totalLen) return false; // need more bytes

    _lastRxMs = millis();

    const uint8_t pktType = (uint8_t)(typeFlags >> 4);
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
            } else {
                setError_("connack_refused");
            }
        }
    } else if (pktType == 9 /* SUBACK */) {
        if (remLen >= 3) {
            const uint16_t pid = (uint16_t)((p[0] << 8) | p[1]);
            (void)pid;
            _awaitingSuback = false;
            _subPacketId = 0;
        }
    } else if (pktType == 3 /* PUBLISH */) {
        // QoS0 only
        const bool retained = (flags & 0x01) != 0;
        if (remLen >= 2) {
            const uint16_t topicLen = (uint16_t)((p[0] << 8) | p[1]);
            if ((uint32_t)(topicLen + 2U) <= remLen && topicLen < TOPIC_MAX) {
                char topic[TOPIC_MAX];
                memcpy(topic, p + 2, topicLen);
                topic[topicLen] = '\0';
                uint16_t off = (uint16_t)(2 + topicLen);
                // QoS>0 includes packet id (not supported); if present, skip minimally.
                if ((flags & 0x06) != 0) {
                    if ((uint32_t)(off + 2U) <= remLen) off += 2;
                }
                const uint16_t payLen = (off <= remLen) ? (uint16_t)(remLen - off) : 0;
                if (_pubCb) {
                    _pubCb(_pubCbCtx, topic, p + off, payLen, retained);
                }
            }
        }
    } else if (pktType == 14 /* DISCONNECT */) {
        setError_("server_disconnect");
    } else {
        // ignore other frames
    }

    // Consume frame from _rx buffer (memmove tail)
    const uint16_t remain = (uint16_t)(_rxLen - totalLen);
    if (remain) memmove(_rx, _rx + totalLen, remain);
    _rxLen = remain;
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
    if (_txLen != 0) return false;
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
    if (total > sizeof(_tx)) return false;

    uint16_t off = 0;
    _tx[off++] = 0x10; // CONNECT
    memcpy(_tx + off, rl, rlLen);
    off += rlLen;
    memcpy(_tx + off, vh, vhLen);
    off += vhLen;
    memcpy(_tx + off, pl, plLen);
    off += plLen;

    _txLen = off;
    _txOff = 0;
    // CONNECT is staged in `_tx` until `pumpWrite_()` actually sends bytes.
    // Do not mark `_lastTxMs` here, otherwise keepalive heuristics think we "talked recently"
    // while the modem may still be buffering/waiting for prompt.
    _lastTxMs = 0;
    return true;
}

bool MqttFsmClient::buildDisconnect_() {
    if (_txLen != 0) return false;
    _tx[0] = 0xE0;
    _tx[1] = 0x00;
    _txLen = 2;
    _txOff = 0;
    _lastTxMs = millis();
    return true;
}

bool MqttFsmClient::buildPingreq_() {
    if (_txLen != 0) return false;
    _tx[0] = 0xC0;
    _tx[1] = 0x00;
    _txLen = 2;
    _txOff = 0;
    _lastTxMs = millis();
    return true;
}

bool MqttFsmClient::buildSubscribe_(const char* topic) {
    if (_txLen != 0) return false;
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
    if (total > sizeof(_tx)) return false;

    uint16_t off = 0;
    _tx[off++] = 0x82; // SUBSCRIBE (type=8, flags=0010)
    memcpy(_tx + off, rl, rlLen);
    off += rlLen;
    off += writeU16_(_tx + off, pid);
    memcpy(_tx + off, pl, plLen);
    off += plLen;

    _txLen = off;
    _txOff = 0;
    _lastTxMs = millis();
    return true;
}

bool MqttFsmClient::buildPublish_(const char* topic, const uint8_t* payload, uint16_t len, bool retained) {
    if (_txLen != 0) return false;
    if (!topic || !topic[0]) return false;
    const uint16_t topicLen = (uint16_t)strnlen(topic, 255);
    if (topicLen == 0 || topicLen > 255) return false;

    const uint32_t remLen = 2 + topicLen + len;
    uint8_t rl[4];
    const uint8_t rlLen = encodeRemainingLen_(remLen, rl);

    const uint16_t total = (uint16_t)(1 + rlLen + 2 + topicLen + len);
    if (total > sizeof(_tx)) {
        logger.log("[MqttFsm] PUBLISH too large: need=%u txMax=%u (topicLen=%u payloadLen=%u)\n",
                   (unsigned)total, (unsigned)sizeof(_tx), (unsigned)topicLen, (unsigned)len);
        return false;
    }

    uint16_t off = 0;
    uint8_t hdr = 0x30; // PUBLISH QoS0
    if (retained) hdr |= 0x01;
    _tx[off++] = hdr;
    memcpy(_tx + off, rl, rlLen);
    off += rlLen;
    _tx[off++] = (uint8_t)(topicLen >> 8);
    _tx[off++] = (uint8_t)(topicLen & 0xFF);
    memcpy(_tx + off, topic, topicLen);
    off += topicLen;
    if (len) {
        memcpy(_tx + off, payload, len);
        off += len;
    }
    _txLen = off;
    _txOff = 0;
    _lastTxMs = millis();
    return true;
}

bool MqttFsmClient::publishPrintedMeasured(const char* topic, JsonPrintEncodeFn encoder, void* ctx,
                                          uint16_t maxPayloadBytes, bool retained, size_t* measuredBytesOut) {
    if (!topic || !topic[0] || !encoder) return false;
    if (_state != State::Connected) return false;
    if (_txLen != 0) return false;

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
    if (total > sizeof(_tx)) {
        logger.log("[MqttFsm] publishJson: packet too large total=%u txMax=%u topicLen=%u json=%u\n",
                   (unsigned)total, (unsigned)sizeof(_tx), (unsigned)topicLen, (unsigned)mj);
        return false;
    }

    uint16_t off = 0;
    uint8_t hdr = 0x30;
    if (retained) hdr |= 0x01;
    _tx[off++] = hdr;
    memcpy(_tx + off, rl, rlLen);
    off += rlLen;
    _tx[off++] = (uint8_t)(topicLen >> 8);
    _tx[off++] = (uint8_t)(topicLen & 0xFF);
    memcpy(_tx + off, topic, topicLen);
    off += topicLen;

    JsonIntoBufferPrint jp(_tx + off, sizeof(_tx) - off);
    const size_t w = encoder(jp, ctx);
    if (jp.overflowed() || w != mj) {
        logger.log("[MqttFsm] publishJson: encode mismatch wrote=%u expected=%u overflow=%u\n", (unsigned)w,
                   (unsigned)mj, jp.overflowed() ? 1U : 0U);
        return false;
    }
    off += (uint16_t)w;

    _txLen = off;
    _txOff = 0;
    _lastTxMs = millis();
    return true;
}

