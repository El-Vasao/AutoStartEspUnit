#include <unity.h>

#include <string.h>

#include "LogSink.h"
#include "modem/AtSession.h"
#include "modem/ModemUart.h"

namespace {
HardwareSerial g_serial;
ModemUart g_uart(g_serial);
AtSession g_at(g_uart);

void onLine(void* ctx, const char* line) { static_cast<AtSession*>(ctx)->onLine(line); }
void onByte(void* ctx, char c) { static_cast<AtSession*>(ctx)->onByte(c); }

AtSession::Request req(const char* cmd, uint8_t expect, uint32_t timeoutMs = 1000, const char* prefix = nullptr,
                       const char* tag = nullptr) {
    AtSession::Request r{};
    r.cmd = cmd;
    r.timeoutMs = timeoutMs;
    r.expectMask = expect;
    r.prefix = prefix;
    r.tag = tag;
    return r;
}

constexpr uint8_t kOk = (uint8_t)AtSession::Expect::Ok;
constexpr uint8_t kPrefix = (uint8_t)AtSession::Expect::LinePrefix;
constexpr uint8_t kPrompt = (uint8_t)AtSession::Expect::Prompt;
constexpr uint8_t kAnyLine = (uint8_t)AtSession::Expect::AnyLine;

void rx(const char* s) {
    g_serial.injectRx(s);
    g_uart.pollRx();
}
} // namespace

void setUp() {
    g_at.reset();
    g_serial.clearTx();
    g_uart.setDataMode(false);
    g_uart.setLineHandler(onLine, &g_at);
    g_uart.setByteHandler(onByte, &g_at);
    test_log::clear();
}

void tearDown() {}

static void test_command_is_sent_on_tick_with_crlf() {
    TEST_ASSERT_TRUE(g_at.enqueue(req("AT+CSQ", kOk)));
    TEST_ASSERT_FALSE(g_at.isBusy());
    g_at.tick(0);
    TEST_ASSERT_TRUE(g_at.isBusy());
    TEST_ASSERT_EQUAL_STRING("AT+CSQ\r\n", g_serial.tx());
    TEST_ASSERT_TRUE(test_log::contains("[AT] >> AT+CSQ"));
}

static void test_prefix_line_is_captured_until_final_ok() {
    g_at.enqueue(req("AT+CSQ", kOk | kPrefix, 1000, "+CSQ:"));
    g_at.tick(0);
    rx("\r\n+CSQ: 17,0\r\n");
    TEST_ASSERT_FALSE(g_at.hasResult());
    rx("\r\nOK\r\n");
    TEST_ASSERT_TRUE(g_at.hasResult());
    const AtSession::Result r = g_at.takeResult();
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.gotPrefix);
    TEST_ASSERT_EQUAL_STRING("+CSQ: 17,0", r.capturedLine);
    TEST_ASSERT_FALSE(g_at.isBusy());
}

static void test_error_and_cme_error_finish_with_error() {
    g_at.enqueue(req("AT+CPIN?", kOk));
    g_at.tick(0);
    rx("ERROR\r\n");
    AtSession::Result r = g_at.takeResult();
    TEST_ASSERT_TRUE(r.error);
    TEST_ASSERT_FALSE(r.ok);

    g_at.enqueue(req("AT+CPIN?", kOk));
    g_at.tick(0);
    rx("+CME ERROR: 10\r\n");
    r = g_at.takeResult();
    TEST_ASSERT_TRUE(r.error);
    TEST_ASSERT_EQUAL_STRING("+CME ERROR: 10", r.capturedLine);
}

static void test_timeout_is_reported_and_logged() {
    g_at.enqueue(req("AT", kOk, 500, nullptr, "probe"));
    g_at.tick(100);
    g_at.tick(599);
    TEST_ASSERT_FALSE(g_at.hasResult());
    g_at.tick(600);
    TEST_ASSERT_TRUE(g_at.hasResult());
    const AtSession::Result r = g_at.takeResult();
    TEST_ASSERT_TRUE(r.timedOut);
    TEST_ASSERT_EQUAL_STRING("probe", r.tag);
    TEST_ASSERT_TRUE(test_log::contains("[AT] << TIMEOUT for AT (probe)"));
}

static void test_high_priority_queue_goes_first() {
    g_at.enqueue(req("AT+NORMAL", kOk));
    g_at.enqueueHigh(req("AT+HIGH", kOk));
    g_at.tick(0);
    TEST_ASSERT_EQUAL_STRING("AT+HIGH\r\n", g_serial.tx());
    rx("OK\r\n");
    g_at.takeResult();
    g_serial.clearTx();
    g_at.tick(1);
    TEST_ASSERT_EQUAL_STRING("AT+NORMAL\r\n", g_serial.tx());
}

static void test_prompt_completes_cipsend_prelude() {
    g_at.enqueue(req("AT+CIPSEND=5", kPrompt));
    g_at.tick(0);
    rx(">");
    TEST_ASSERT_TRUE(g_at.hasResult());
    TEST_ASSERT_TRUE(g_at.takeResult().gotPrompt);
}

static void test_any_line_finishes_on_first_line() {
    g_at.enqueue(req("AT+GSN", kAnyLine));
    g_at.tick(0);
    rx("861234567890123\r\n");
    const AtSession::Result r = g_at.takeResult();
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("861234567890123", r.capturedLine);
}

static void test_queued_cmd_is_copied_not_aliased() {
    char buf[16];
    strlcpy(buf, "AT+CSQ", sizeof(buf));
    TEST_ASSERT_TRUE(g_at.enqueue(req(buf, kOk)));
    strlcpy(buf, "AT+COPS?", sizeof(buf));
    TEST_ASSERT_TRUE(g_at.enqueue(req(buf, kOk)));
    g_at.tick(0);
    TEST_ASSERT_EQUAL_STRING("AT+CSQ\r\n", g_serial.tx());
    rx("OK\r\n");
    g_at.takeResult();
    g_serial.clearTx();
    g_at.tick(1);
    TEST_ASSERT_EQUAL_STRING("AT+COPS?\r\n", g_serial.tx());
}

static void test_queue_full_is_rejected() {
    for (uint8_t i = 0; i < Gsm::AT_QSIZE; i++) TEST_ASSERT_TRUE(g_at.enqueue(req("AT", kOk)));
    TEST_ASSERT_FALSE(g_at.enqueue(req("AT", kOk)));
    for (uint8_t i = 0; i < Gsm::AT_HQSIZE; i++) TEST_ASSERT_TRUE(g_at.enqueueHigh(req("AT", kOk)));
    TEST_ASSERT_FALSE(g_at.enqueueHigh(req("AT", kOk)));
}

static void test_lines_are_ignored_when_idle() {
    rx("OK\r\n");
    TEST_ASSERT_FALSE(g_at.hasResult());
    TEST_ASSERT_FALSE(g_at.isBusy());
}

static void test_uart_frames_lines_and_skips_whitespace_noise() {
    g_at.enqueue(req("AT+CREG?", kOk | kPrefix, 1000, "+CREG:"));
    g_at.tick(0);
    rx("   \r\n+CREG: 0,1\r\nOK\r\n");
    const AtSession::Result r = g_at.takeResult();
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("+CREG: 0,1", r.capturedLine);
}

static void test_data_mode_bypasses_line_framing() {
    g_at.enqueue(req("AT", kOk));
    g_at.tick(0);
    g_uart.setDataMode(true);
    rx("OK\r\n");
    TEST_ASSERT_FALSE(g_at.hasResult());
    g_uart.setDataMode(false);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_command_is_sent_on_tick_with_crlf);
    RUN_TEST(test_prefix_line_is_captured_until_final_ok);
    RUN_TEST(test_error_and_cme_error_finish_with_error);
    RUN_TEST(test_timeout_is_reported_and_logged);
    RUN_TEST(test_high_priority_queue_goes_first);
    RUN_TEST(test_prompt_completes_cipsend_prelude);
    RUN_TEST(test_any_line_finishes_on_first_line);
    RUN_TEST(test_queued_cmd_is_copied_not_aliased);
    RUN_TEST(test_queue_full_is_rejected);
    RUN_TEST(test_lines_are_ignored_when_idle);
    RUN_TEST(test_uart_frames_lines_and_skips_whitespace_noise);
    RUN_TEST(test_data_mode_bypasses_line_framing);
    return UNITY_END();
}
