// src/mqtt/MqttWire.cpp
#include "mqtt/MqttWire.h"

#include <string.h>

namespace mqtt_wire {

namespace {
uint16_t putU16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
    return 2;
}

uint16_t readU16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

size_t mqttStrLen(const char* s) { return s ? strlen(s) : 0; }

/// Whole packet size for a Remaining Length, or 0 when it is not encodable.
uint32_t packetSize(uint32_t remLen) {
    const uint8_t rl = remainingLengthSize(remLen);
    return rl ? 1U + rl + remLen : 0;
}

/// Fixed header (type/flags + Remaining Length); the caller has checked that the packet fits.
uint16_t writeFixedHeader(uint8_t* out, uint8_t typeFlags, uint32_t remLen) {
    uint8_t rl[4];
    const uint8_t n = encodeRemainingLength(remLen, rl);
    out[0] = typeFlags;
    memcpy(out + 1, rl, n);
    return (uint16_t)(1 + n);
}
} // namespace

uint8_t encodeRemainingLength(uint32_t len, uint8_t out[4]) {
    if (len > REMAINING_LENGTH_MAX) return 0;
    uint8_t i = 0;
    do {
        uint8_t d = (uint8_t)(len % 128);
        len /= 128;
        if (len > 0) d |= 0x80;
        out[i++] = d;
    } while (len > 0);
    return i;
}

uint8_t remainingLengthSize(uint32_t len) {
    if (len > REMAINING_LENGTH_MAX) return 0;
    if (len < 128UL) return 1;
    if (len < 16384UL) return 2;
    if (len < 2097152UL) return 3;
    return 4;
}

Decode decodeRemainingLength(const uint8_t* p, uint16_t cap, uint32_t& outLen, uint8_t& outUsed) {
    uint32_t multiplier = 1;
    uint32_t value = 0;
    for (uint8_t i = 0; i < 4; i++) {
        if (i >= cap) return Decode::NeedMore;
        const uint8_t d = p[i];
        value += (uint32_t)(d & 127) * multiplier;
        multiplier *= 128;
        if ((d & 128) == 0) {
            outLen = value;
            outUsed = (uint8_t)(i + 1);
            return Decode::Ok;
        }
    }
    return Decode::Malformed;
}

uint16_t putString(uint8_t* p, uint16_t cap, const char* s) {
    const size_t len = mqttStrLen(s);
    if (len > 0xFFFF || (size_t)cap < 2 + len) return 0;
    putU16(p, (uint16_t)len);
    if (len) memcpy(p + 2, s, len);
    return (uint16_t)(2 + len);
}

Head parseHead(const uint8_t* buf, uint16_t len, FrameHead& out) {
    if (len < 1) return Head::NeedMore;
    const uint8_t type = (uint8_t)(buf[0] >> 4);
    if (type < 1 || type > 14) return Head::BadType;
    if (len < 2) return Head::NeedMore;
    uint32_t remLen = 0;
    uint8_t used = 0;
    switch (decodeRemainingLength(buf + 1, (uint16_t)(len - 1), remLen, used)) {
        case Decode::NeedMore: return Head::NeedMore;
        case Decode::Malformed: return Head::BadRemLen;
        case Decode::Ok: break;
    }
    if (remLen > REMAINING_LENGTH_MAX) return Head::BadRemLen;
    out.type = type;
    out.flags = (uint8_t)(buf[0] & 0x0F);
    out.headerLen = (uint16_t)(1 + used);
    out.remLen = remLen;
    out.totalLen = (uint32_t)out.headerLen + remLen;
    return Head::Ok;
}

bool isValidIncomplete(const uint8_t* buf, uint16_t len, uint16_t capacity, uint32_t& needOut) {
    needOut = 0;
    if (len < 2) return false;
    FrameHead h;
    if (parseHead(buf, len, h) != Head::Ok) return false;
    if (h.totalLen > capacity || (uint32_t)len >= h.totalLen) return false;
    needOut = h.totalLen;
    return true;
}

uint16_t buildConnect(uint8_t* out, uint16_t cap, const ConnectOptions& o) {
    if (!o.clientId || !o.clientId[0]) return 0;
    const bool hasWill = o.willTopic && o.willTopic[0] && o.willMsg;
    const bool hasUser = o.username && o.username[0];
    const bool hasPass = hasUser && o.password && o.password[0];
    const char* protoName = o.mqtt311 ? "MQTT" : "MQIsdp";

    uint32_t remLen = 2 + mqttStrLen(protoName) + 1 + 1 + 2;
    remLen += 2 + mqttStrLen(o.clientId);
    if (hasWill) remLen += 2 + mqttStrLen(o.willTopic) + 2 + mqttStrLen(o.willMsg);
    if (hasUser) remLen += 2 + mqttStrLen(o.username);
    if (hasPass) remLen += 2 + mqttStrLen(o.password);

    const uint32_t total = packetSize(remLen);
    if (!total || total > cap) return 0;

    uint16_t off = writeFixedHeader(out, 0x10, remLen);
    off += putString(out + off, (uint16_t)(cap - off), protoName);
    out[off++] = o.mqtt311 ? 0x04 : 0x03;
    uint8_t flags = 0;
    if (o.cleanSession) flags |= 0x02;
    if (hasWill) {
        flags |= 0x04;
        flags |= (uint8_t)((o.willQos & 3u) << 3);
        if (o.willRetain) flags |= 0x20;
    }
    if (hasUser) flags |= 0x80;
    if (hasPass) flags |= 0x40;
    out[off++] = flags;
    off += putU16(out + off, o.keepAliveSec);
    off += putString(out + off, (uint16_t)(cap - off), o.clientId);
    if (hasWill) {
        off += putString(out + off, (uint16_t)(cap - off), o.willTopic);
        off += putString(out + off, (uint16_t)(cap - off), o.willMsg);
    }
    if (hasUser) off += putString(out + off, (uint16_t)(cap - off), o.username);
    if (hasPass) off += putString(out + off, (uint16_t)(cap - off), o.password);
    return off;
}

uint16_t buildSubscribe(uint8_t* out, uint16_t cap, uint16_t packetId, const char* topic) {
    if (!topic || !topic[0] || packetId == 0) return 0;
    const size_t topicLen = strlen(topic);
    if (topicLen > 0xFFFF) return 0;
    const uint32_t remLen = 2 + 2 + (uint32_t)topicLen + 1;
    const uint32_t total = packetSize(remLen);
    if (!total || total > cap) return 0;
    uint16_t off = writeFixedHeader(out, 0x82, remLen);  // SUBSCRIBE, flags 0010
    off += putU16(out + off, packetId);
    off += putString(out + off, (uint16_t)(cap - off), topic);
    out[off++] = 0x00;  // requested QoS0
    return off;
}

uint16_t publishHeadLength(uint16_t topicLen, uint32_t payloadLen) {
    const uint32_t remLen = 2U + (uint32_t)topicLen + payloadLen;
    const uint8_t rl = remainingLengthSize(remLen);
    if (rl == 0) return 0;
    return (uint16_t)(1U + rl + 2U + topicLen);
}

uint16_t writePublishHead(uint8_t* out, uint16_t cap, const char* topic, uint16_t topicLen, uint32_t payloadLen,
                          bool retained) {
    if (!topic || topicLen == 0) return 0;
    const uint16_t head = publishHeadLength(topicLen, payloadLen);
    if (!head || head > cap) return 0;
    uint16_t off = writeFixedHeader(out, (uint8_t)(retained ? 0x31 : 0x30), 2U + (uint32_t)topicLen + payloadLen);
    off += putU16(out + off, topicLen);
    memcpy(out + off, topic, topicLen);
    return (uint16_t)(off + topicLen);
}

uint16_t buildPublish(uint8_t* out, uint16_t cap, const char* topic, const uint8_t* payload, uint16_t len,
                      bool retained) {
    if (!topic || (!payload && len)) return 0;
    const size_t topicLen = strlen(topic);
    if (topicLen == 0 || topicLen > 0xFFFF) return 0;
    const uint16_t head = publishHeadLength((uint16_t)topicLen, len);
    if (!head || (uint32_t)head + len > cap) return 0;
    writePublishHead(out, cap, topic, (uint16_t)topicLen, len, retained);
    if (len) memcpy(out + head, payload, len);
    return (uint16_t)(head + len);
}

uint16_t buildPuback(uint8_t* out, uint16_t cap, uint16_t packetId) {
    if (cap < 4 || packetId == 0) return 0;
    out[0] = 0x40;
    out[1] = 0x02;
    putU16(out + 2, packetId);
    return 4;
}

uint16_t buildPingreq(uint8_t* out, uint16_t cap) {
    if (cap < 2) return 0;
    out[0] = 0xC0;
    out[1] = 0x00;
    return 2;
}

uint16_t buildDisconnect(uint8_t* out, uint16_t cap) {
    if (cap < 2) return 0;
    out[0] = 0xE0;
    out[1] = 0x00;
    return 2;
}

bool parseConnack(const uint8_t* body, uint32_t remLen, uint8_t& returnCode) {
    if (remLen < 2) return false;
    returnCode = body[1];
    return true;
}

bool parseSuback(const uint8_t* body, uint32_t remLen, uint16_t& packetId, uint8_t& returnCode) {
    if (remLen < 3) return false;
    packetId = readU16(body);
    returnCode = body[2];
    return true;
}

bool parsePublish(uint8_t flags, const uint8_t* body, uint32_t remLen, PublishView& out) {
    out = PublishView{};
    if (remLen < 2 || remLen > 0xFFFF) return false;
    out.qos = (uint8_t)((flags >> 1) & 0x03);
    out.retained = (flags & 0x01) != 0;
    if (out.qos == 3) return false;
    const uint16_t topicLen = readU16(body);
    if (topicLen == 0 || 2U + (uint32_t)topicLen > remLen) return false;
    out.topicOffset = 2;
    out.topicLen = topicLen;
    uint16_t off = (uint16_t)(2 + topicLen);
    if (out.qos > 0) {
        if ((uint32_t)off + 2U > remLen) return false;
        out.packetId = readU16(body + off);
        off += 2;
    }
    out.payloadOffset = off;
    out.payloadLen = (uint16_t)(remLen - off);
    return true;
}

} // namespace mqtt_wire
