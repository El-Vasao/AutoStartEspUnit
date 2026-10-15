#include <unity.h>

#include <string.h>

#include "modem/Sim800Lines.h"

using sim800_lines::IpdFramer;
using sim800_lines::isControlLine;
using Ev = IpdFramer::Event;

static bool ctl(const char* s) { return isControlLine(s, strlen(s)); }

void setUp() {}
void tearDown() {}

static void test_control_lines_finals_and_echo() {
    TEST_ASSERT_TRUE(ctl("OK\r\n"));
    TEST_ASSERT_TRUE(ctl("ERROR\r\n"));
    TEST_ASSERT_TRUE(ctl("AT+CSQ\r\n"));
    TEST_ASSERT_TRUE(ctl("ATE0\r\n"));
    TEST_ASSERT_TRUE(ctl("SEND OK\r\n"));
    TEST_ASSERT_TRUE(ctl("SEND FAIL\r\n"));
    TEST_ASSERT_TRUE(ctl("CONNECT OK\r\n"));
    TEST_ASSERT_TRUE(ctl("CONNECT FAIL\r\n"));
    TEST_ASSERT_TRUE(ctl("ALREADY CONNECT\r\n"));
    TEST_ASSERT_TRUE(ctl("CLOSE OK\r\n"));
    TEST_ASSERT_TRUE(ctl("SHUT OK\r\n"));
    TEST_ASSERT_TRUE(ctl(">\r\n"));
}

static void test_control_lines_urcs() {
    TEST_ASSERT_TRUE(ctl("+CSQ: 17,0\r\n"));
    TEST_ASSERT_TRUE(ctl("+CREG: 1\r\n"));
    TEST_ASSERT_TRUE(ctl("+PDP: DEACT\r\n"));
    TEST_ASSERT_TRUE(ctl("RDY\r\n"));
    TEST_ASSERT_TRUE(ctl("CLOSED\r\n"));
    TEST_ASSERT_TRUE(ctl("RING\r\n"));
    TEST_ASSERT_TRUE(ctl("NO CARRIER\r\n"));
    TEST_ASSERT_TRUE(ctl("BUSY\r\n"));
    TEST_ASSERT_TRUE(ctl("NO ANSWER\r\n"));
    TEST_ASSERT_TRUE(ctl("Call Ready\r\n"));
    TEST_ASSERT_TRUE(ctl("SMS Ready\r\n"));
    // Forms with '+' eaten by an aborted +IPD match.
    TEST_ASSERT_TRUE(ctl("CSQ: 17,0\r\n"));
    TEST_ASSERT_TRUE(ctl("CREG: 1\r\n"));
    TEST_ASSERT_TRUE(ctl("COPS: 0\r\n"));
    TEST_ASSERT_TRUE(ctl("IPD,4\r\n"));
}

static void test_empty_and_whitespace_lines_are_control() {
    TEST_ASSERT_TRUE(isControlLine(nullptr, 0));
    TEST_ASSERT_TRUE(ctl(""));
    TEST_ASSERT_TRUE(ctl("\r\n"));
    TEST_ASSERT_TRUE(ctl("  \r\n"));
}

static void test_payload_like_lines_are_not_control() {
    TEST_ASSERT_FALSE(ctl("hello\r\n"));
    TEST_ASSERT_FALSE(ctl("OKAY\r\n"));
    TEST_ASSERT_FALSE(ctl("{\"cmd\":\"run\"}\r\n"));
}

static void feedAll(IpdFramer& f, const char* s, Ev* events) {
    for (size_t i = 0; s[i]; i++) events[i] = f.feed(s[i]);
}

static void test_ipd_frame_events() {
    IpdFramer f;
    Ev ev[16];
    feedAll(f, "+IPD,3:ABC", ev);
    for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT8((uint8_t)Ev::Consumed, (uint8_t)ev[i]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ev::DataStart, (uint8_t)ev[6]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ev::Payload, (uint8_t)ev[7]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ev::Payload, (uint8_t)ev[8]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ev::PayloadEnd, (uint8_t)ev[9]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)IpdFramer::State::Idle, (uint8_t)f.state());
    TEST_ASSERT_EQUAL_UINT16(3, f.length());
}

static void test_ipd_payload_counters() {
    IpdFramer f;
    for (const char* p = "+IPD,4:"; *p; ++p) f.feed(*p);
    TEST_ASSERT_TRUE(f.inPayload());
    TEST_ASSERT_EQUAL_UINT16(4, f.length());
    f.feed('x');
    f.feed('y');
    TEST_ASSERT_EQUAL_UINT16(2, f.received());
    TEST_ASSERT_TRUE(f.inPayload());
}

static void test_ipd_length_tolerates_whitespace() {
    IpdFramer f;
    Ev last = Ev::Consumed;
    for (const char* p = "+IPD, 12\r\n:"; *p; ++p) last = f.feed(*p);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ev::DataStart, (uint8_t)last);
    TEST_ASSERT_EQUAL_UINT16(12, f.length());
}

static void test_ipd_length_saturates() {
    IpdFramer f;
    for (const char* p = "+IPD,99999:"; *p; ++p) f.feed(*p);
    TEST_ASSERT_EQUAL_UINT16(IpdFramer::kMaxIpdLen, f.length());
}

static void test_ipd_zero_length_does_not_consume_payload_byte() {
    IpdFramer f;
    Ev last = Ev::Consumed;
    for (const char* p = "+IPD,0:"; *p; ++p) last = f.feed(*p);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ev::DataStart, (uint8_t)last);
    TEST_ASSERT_EQUAL_UINT16(0, f.length());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)IpdFramer::State::Idle, (uint8_t)f.state());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ev::Raw, (uint8_t)f.feed('X'));
}

static void test_aborted_header_replays_consumed_bytes() {
    IpdFramer f;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ev::Consumed, (uint8_t)f.feed('+'));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ev::Raw, (uint8_t)f.feed('C'));
    TEST_ASSERT_EQUAL_STRING("+", f.replay());

    for (const char* p = "+IP"; *p; ++p) f.feed(*p);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ev::Raw, (uint8_t)f.feed('X'));
    TEST_ASSERT_EQUAL_STRING("+IP", f.replay());

    for (const char* p = "+IPD,12"; *p; ++p) f.feed(*p);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ev::Raw, (uint8_t)f.feed('x'));
    TEST_ASSERT_EQUAL_STRING("+IPD,12", f.replay());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)IpdFramer::State::Idle, (uint8_t)f.state());
}

static void test_plain_byte_is_raw_without_replay() {
    IpdFramer f;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ev::Raw, (uint8_t)f.feed('O'));
    TEST_ASSERT_EQUAL_STRING("", f.replay());
}

static void test_reset_clears_partial_frame() {
    IpdFramer f;
    for (const char* p = "+IPD,5:ab"; *p; ++p) f.feed(*p);
    f.reset();
    TEST_ASSERT_FALSE(f.inPayload());
    TEST_ASSERT_EQUAL_UINT16(0, f.length());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ev::Raw, (uint8_t)f.feed('a'));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_control_lines_finals_and_echo);
    RUN_TEST(test_control_lines_urcs);
    RUN_TEST(test_empty_and_whitespace_lines_are_control);
    RUN_TEST(test_payload_like_lines_are_not_control);
    RUN_TEST(test_ipd_frame_events);
    RUN_TEST(test_ipd_payload_counters);
    RUN_TEST(test_ipd_length_tolerates_whitespace);
    RUN_TEST(test_ipd_length_saturates);
    RUN_TEST(test_ipd_zero_length_does_not_consume_payload_byte);
    RUN_TEST(test_aborted_header_replays_consumed_bytes);
    RUN_TEST(test_plain_byte_is_raw_without_replay);
    RUN_TEST(test_reset_clears_partial_frame);
    return UNITY_END();
}
