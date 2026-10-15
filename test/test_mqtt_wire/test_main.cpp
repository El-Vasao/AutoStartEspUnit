#include <unity.h>

#include <string.h>

#include "mqtt/MqttWire.h"

using namespace mqtt_wire;

void setUp() {}
void tearDown() {}

static void test_remaining_length_round_trip() {
    const uint32_t values[] = {0, 1, 127, 128, 16383, 16384, 2097151, 2097152, REMAINING_LENGTH_MAX};
    const uint8_t sizes[] = {1, 1, 1, 2, 2, 3, 3, 4, 4};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        uint8_t buf[4];
        const uint8_t n = encodeRemainingLength(values[i], buf);
        TEST_ASSERT_EQUAL_UINT8(sizes[i], n);
        TEST_ASSERT_EQUAL_UINT8(sizes[i], remainingLengthSize(values[i]));
        uint32_t back = 0;
        uint8_t used = 0;
        TEST_ASSERT_EQUAL_UINT8((uint8_t)Decode::Ok, (uint8_t)decodeRemainingLength(buf, n, back, used));
        TEST_ASSERT_EQUAL_UINT32(values[i], back);
        TEST_ASSERT_EQUAL_UINT8(n, used);
    }
}

static void test_remaining_length_too_large_is_rejected() {
    uint8_t buf[4];
    TEST_ASSERT_EQUAL_UINT8(0, encodeRemainingLength(REMAINING_LENGTH_MAX + 1, buf));
    TEST_ASSERT_EQUAL_UINT8(0, remainingLengthSize(REMAINING_LENGTH_MAX + 1));
}

static void test_remaining_length_decode_need_more_and_malformed() {
    uint32_t v = 0;
    uint8_t used = 0;
    const uint8_t partial[] = {0x80, 0x80};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Decode::NeedMore, (uint8_t)decodeRemainingLength(partial, 2, v, used));
    const uint8_t bad[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x01};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Decode::Malformed, (uint8_t)decodeRemainingLength(bad, 5, v, used));
}

static void test_put_string_does_not_truncate() {
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_UINT16(5, putString(buf, sizeof(buf), "abc"));
    TEST_ASSERT_EQUAL_UINT8(0, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(3, buf[1]);
    TEST_ASSERT_EQUAL_MEMORY("abc", buf + 2, 3);
    TEST_ASSERT_EQUAL_UINT16(0, putString(buf, sizeof(buf), "too long text"));
    TEST_ASSERT_EQUAL_UINT16(2, putString(buf, sizeof(buf), nullptr));
}

static void test_parse_head_cases() {
    FrameHead h;
    const uint8_t ok[] = {0x30, 0x05, 0, 1, 't', 'x', 'y'};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Head::Ok, (uint8_t)parseHead(ok, sizeof(ok), h));
    TEST_ASSERT_EQUAL_UINT8(packet::Publish, h.type);
    TEST_ASSERT_EQUAL_UINT16(2, h.headerLen);
    TEST_ASSERT_EQUAL_UINT32(5, h.remLen);
    TEST_ASSERT_EQUAL_UINT32(7, h.totalLen);

    const uint8_t junk[] = {0x00, 0x30};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Head::BadType, (uint8_t)parseHead(junk, sizeof(junk), h));
    const uint8_t reserved15[] = {0xF0, 0x00};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Head::BadType, (uint8_t)parseHead(reserved15, sizeof(reserved15), h));
    const uint8_t one[] = {0x30};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Head::NeedMore, (uint8_t)parseHead(one, sizeof(one), h));
    const uint8_t badRl[] = {0x30, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Head::BadRemLen, (uint8_t)parseHead(badRl, sizeof(badRl), h));
}

static void test_valid_incomplete_detection() {
    uint32_t need = 0;
    const uint8_t head[] = {0x30, 0x14};  // PUBLISH, remaining 20, nothing else yet
    TEST_ASSERT_TRUE(isValidIncomplete(head, sizeof(head), 256, need));
    TEST_ASSERT_EQUAL_UINT32(22, need);
    TEST_ASSERT_FALSE(isValidIncomplete(head, sizeof(head), 16, need));  // would not fit
    const uint8_t junk[] = {0x00, 0x30, 0x02};
    TEST_ASSERT_FALSE(isValidIncomplete(junk, sizeof(junk), 256, need));
    const uint8_t complete[] = {0xD0, 0x00};  // PINGRESP
    TEST_ASSERT_FALSE(isValidIncomplete(complete, sizeof(complete), 256, need));
}

static void test_connect_311_with_will_and_credentials() {
    uint8_t buf[128];
    ConnectOptions o;
    o.mqtt311 = true;
    o.clientId = "dev1";
    o.username = "u";
    o.password = "p";
    o.willTopic = "t/avail";
    o.willMsg = "offline";
    o.willRetain = true;
    o.willQos = 1;
    o.keepAliveSec = 30;
    o.cleanSession = true;
    const uint16_t n = buildConnect(buf, sizeof(buf), o);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_HEX8(0x10, buf[0]);
    FrameHead h;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Head::Ok, (uint8_t)parseHead(buf, n, h));
    TEST_ASSERT_EQUAL_UINT32(n, h.totalLen);
    const uint8_t* vh = buf + h.headerLen;
    TEST_ASSERT_EQUAL_MEMORY("\x00\x04MQTT", vh, 6);
    TEST_ASSERT_EQUAL_HEX8(0x04, vh[6]);
    // user | pass | will retain | will qos1 | will | clean
    TEST_ASSERT_EQUAL_HEX8(0x80 | 0x40 | 0x20 | 0x08 | 0x04 | 0x02, vh[7]);
    TEST_ASSERT_EQUAL_UINT8(0, vh[8]);
    TEST_ASSERT_EQUAL_UINT8(30, vh[9]);
    TEST_ASSERT_EQUAL_MEMORY("\x00\x04" "dev1", vh + 10, 6);
}

static void test_connect_31_protocol_name() {
    uint8_t buf[64];
    ConnectOptions o;
    o.mqtt311 = false;
    o.clientId = "c";
    const uint16_t n = buildConnect(buf, sizeof(buf), o);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_MEMORY("\x00\x06MQIsdp", buf + 2, 8);
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[10]);
}

static void test_connect_password_requires_username() {
    uint8_t buf[64];
    ConnectOptions o;
    o.clientId = "c";
    o.password = "secret";
    const uint16_t n = buildConnect(buf, sizeof(buf), o);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[2 + 6 + 1]);  // only clean session
}

static void test_connect_requires_client_id_and_room() {
    uint8_t buf[16];
    ConnectOptions o;
    TEST_ASSERT_EQUAL_UINT16(0, buildConnect(buf, sizeof(buf), o));
    o.clientId = "a-client-id-that-does-not-fit";
    memset(buf, 0xAA, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT16(0, buildConnect(buf, sizeof(buf), o));
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[0]);  // untouched on failure
}

static void test_subscribe_packet() {
    uint8_t buf[32];
    const uint16_t n = buildSubscribe(buf, sizeof(buf), 0x1234, "a/b");
    const uint8_t expect[] = {0x82, 0x08, 0x12, 0x34, 0x00, 0x03, 'a', '/', 'b', 0x00};
    TEST_ASSERT_EQUAL_UINT16(sizeof(expect), n);
    TEST_ASSERT_EQUAL_MEMORY(expect, buf, sizeof(expect));
    TEST_ASSERT_EQUAL_UINT16(0, buildSubscribe(buf, sizeof(buf), 0, "a/b"));
}

static void test_publish_packet_and_retain_flag() {
    uint8_t buf[32];
    const uint8_t payload[] = {'o', 'n'};
    const uint16_t n = buildPublish(buf, sizeof(buf), "t", payload, 2, true);
    const uint8_t expect[] = {0x31, 0x05, 0x00, 0x01, 't', 'o', 'n'};
    TEST_ASSERT_EQUAL_UINT16(sizeof(expect), n);
    TEST_ASSERT_EQUAL_MEMORY(expect, buf, sizeof(expect));
    TEST_ASSERT_EQUAL_UINT16(5, publishHeadLength(1, 2));
}

static void test_publish_failure_leaves_buffer_untouched() {
    uint8_t buf[8];
    memset(buf, 0x5A, sizeof(buf));
    const uint8_t payload[16] = {0};
    TEST_ASSERT_EQUAL_UINT16(0, buildPublish(buf, sizeof(buf), "topic", payload, sizeof(payload), false));
    for (size_t i = 0; i < sizeof(buf); i++) TEST_ASSERT_EQUAL_HEX8(0x5A, buf[i]);
}

static void test_publish_head_with_two_byte_remaining_length() {
    uint8_t buf[16];
    const uint16_t head = writePublishHead(buf, sizeof(buf), "t", 1, 200, false);
    TEST_ASSERT_EQUAL_UINT16(publishHeadLength(1, 200), head);
    TEST_ASSERT_EQUAL_UINT16(6, head);  // type + 2-byte RL + topic length + "t"
    uint32_t rem = 0;
    uint8_t used = 0;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Decode::Ok, (uint8_t)decodeRemainingLength(buf + 1, 4, rem, used));
    TEST_ASSERT_EQUAL_UINT32(203, rem);
}

static void test_small_control_packets() {
    uint8_t buf[4];
    TEST_ASSERT_EQUAL_UINT16(2, buildPingreq(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0xC0, buf[0]);
    TEST_ASSERT_EQUAL_UINT16(2, buildDisconnect(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0xE0, buf[0]);
    TEST_ASSERT_EQUAL_UINT16(4, buildPuback(buf, sizeof(buf), 7));
    const uint8_t expect[] = {0x40, 0x02, 0x00, 0x07};
    TEST_ASSERT_EQUAL_MEMORY(expect, buf, 4);
    TEST_ASSERT_EQUAL_UINT16(0, buildPuback(buf, sizeof(buf), 0));
}

static void test_parse_connack_and_suback() {
    uint8_t rc = 0xFF;
    const uint8_t connack[] = {0x00, 0x05};
    TEST_ASSERT_TRUE(parseConnack(connack, 2, rc));
    TEST_ASSERT_EQUAL_UINT8(5, rc);
    TEST_ASSERT_FALSE(parseConnack(connack, 1, rc));

    uint16_t pid = 0;
    const uint8_t suback[] = {0x00, 0x2A, 0x80};
    TEST_ASSERT_TRUE(parseSuback(suback, 3, pid, rc));
    TEST_ASSERT_EQUAL_UINT16(42, pid);
    TEST_ASSERT_EQUAL_HEX8(0x80, rc);
    TEST_ASSERT_FALSE(parseSuback(suback, 2, pid, rc));
}

static void test_parse_publish_qos0_and_qos1() {
    PublishView v;
    const uint8_t q0[] = {0x00, 0x03, 'a', '/', 'b', 'h', 'i'};
    TEST_ASSERT_TRUE(parsePublish(0x01, q0, sizeof(q0), v));
    TEST_ASSERT_TRUE(v.retained);
    TEST_ASSERT_EQUAL_UINT8(0, v.qos);
    TEST_ASSERT_EQUAL_UINT16(2, v.topicOffset);
    TEST_ASSERT_EQUAL_UINT16(3, v.topicLen);
    TEST_ASSERT_EQUAL_UINT16(5, v.payloadOffset);
    TEST_ASSERT_EQUAL_UINT16(2, v.payloadLen);

    const uint8_t q1[] = {0x00, 0x01, 't', 0x00, 0x09, 'x'};
    TEST_ASSERT_TRUE(parsePublish(0x02, q1, sizeof(q1), v));
    TEST_ASSERT_EQUAL_UINT8(1, v.qos);
    TEST_ASSERT_EQUAL_UINT16(9, v.packetId);
    TEST_ASSERT_EQUAL_UINT16(5, v.payloadOffset);
    TEST_ASSERT_EQUAL_UINT16(1, v.payloadLen);
}

static void test_parse_publish_rejects_malformed() {
    PublishView v;
    const uint8_t emptyTopic[] = {0x00, 0x00, 'x'};
    TEST_ASSERT_FALSE(parsePublish(0x00, emptyTopic, sizeof(emptyTopic), v));
    const uint8_t topicPastEnd[] = {0x00, 0x09, 'a'};
    TEST_ASSERT_FALSE(parsePublish(0x00, topicPastEnd, sizeof(topicPastEnd), v));
    const uint8_t q1NoPid[] = {0x00, 0x01, 't', 0x00};
    TEST_ASSERT_FALSE(parsePublish(0x02, q1NoPid, sizeof(q1NoPid), v));
    const uint8_t q3[] = {0x00, 0x01, 't'};
    TEST_ASSERT_FALSE(parsePublish(0x06, q3, sizeof(q3), v));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_remaining_length_round_trip);
    RUN_TEST(test_remaining_length_too_large_is_rejected);
    RUN_TEST(test_remaining_length_decode_need_more_and_malformed);
    RUN_TEST(test_put_string_does_not_truncate);
    RUN_TEST(test_parse_head_cases);
    RUN_TEST(test_valid_incomplete_detection);
    RUN_TEST(test_connect_311_with_will_and_credentials);
    RUN_TEST(test_connect_31_protocol_name);
    RUN_TEST(test_connect_password_requires_username);
    RUN_TEST(test_connect_requires_client_id_and_room);
    RUN_TEST(test_subscribe_packet);
    RUN_TEST(test_publish_packet_and_retain_flag);
    RUN_TEST(test_publish_failure_leaves_buffer_untouched);
    RUN_TEST(test_publish_head_with_two_byte_remaining_length);
    RUN_TEST(test_small_control_packets);
    RUN_TEST(test_parse_connack_and_suback);
    RUN_TEST(test_parse_publish_qos0_and_qos1);
    RUN_TEST(test_parse_publish_rejects_malformed);
    return UNITY_END();
}
