#include <unity.h>

#include <string.h>

#include "LogSink.h"
#include "common/Logger.h"

void setUp() {
    test_log::clear();
    test_clock::set(0);
    logger_test::g_foreignTask = false;
    logger.flushDeferred();
    test_log::clear();
}

void tearDown() {}

static void test_line_has_millis_and_module() {
    test_clock::set(1234);
    logger.log(LogModule::FsManager, "mounted %d files\n", 7);
    TEST_ASSERT_EQUAL_UINT32(1, test_log::count());
    TEST_ASSERT_EQUAL_STRING("[1234ms] [FsManager] mounted 7 files\n", test_log::last());
}

static void test_every_module_has_unique_name() {
    const size_t n = static_cast<size_t>(LogModule::Count);
    for (size_t i = 0; i < n; i++) {
        const char* a = logModuleName(static_cast<LogModule>(i));
        TEST_ASSERT_NOT_NULL(a);
        TEST_ASSERT_TRUE(strlen(a) > 0);
        TEST_ASSERT_TRUE(strcmp(a, "?") != 0);
        for (size_t j = i + 1; j < n; j++) {
            TEST_ASSERT_TRUE_MESSAGE(strcmp(a, logModuleName(static_cast<LogModule>(j))) != 0, a);
        }
    }
}

static void test_at_channel_keeps_ui_filter_token() {
    logger.log(LogModule::AT, ">> AT+CSQ\n");
    TEST_ASSERT_NOT_NULL(strstr(test_log::last(), "] [AT] "));
}

static void test_truncated_line_ends_with_newline() {
    char longText[400];
    memset(longText, 'x', sizeof(longText) - 1);
    longText[sizeof(longText) - 1] = '\0';
    logger.log(LogModule::Core, "%s\n", longText);
    const char* s = test_log::last();
    TEST_ASSERT_EQUAL_UINT32(Logging::MAX_MESSAGE_LENGTH - 1, strlen(s));
    TEST_ASSERT_EQUAL_CHAR('\n', s[strlen(s) - 1]);
}

static size_t formatInto(char* out, size_t cap, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const size_t n = logFormatLine(out, cap, 5, LogModule::Config, fmt, args);
    va_end(args);
    return n;
}

static void test_format_line_helper() {
    char out[32];
    const size_t n = formatInto(out, sizeof(out), "ok %d\n", 1);
    TEST_ASSERT_EQUAL_STRING("[5ms] [Config] ok 1\n", out);
    TEST_ASSERT_EQUAL_UINT32(strlen(out), n);

    char tiny[12];
    const size_t t = formatInto(tiny, sizeof(tiny), "long text\n");
    TEST_ASSERT_EQUAL_UINT32(sizeof(tiny) - 1, t);
    TEST_ASSERT_EQUAL_CHAR('\n', tiny[sizeof(tiny) - 2]);
}

static void test_foreign_task_lines_are_deferred_until_flush() {
    logger_test::g_foreignTask = true;
    logger.log(LogModule::WebServer, "from async 1\n");
    logger.log(LogModule::WebServer, "from async 2\n");
    TEST_ASSERT_EQUAL_UINT32(0, test_log::count());

    logger_test::g_foreignTask = false;
    logger.flushDeferred();
    TEST_ASSERT_EQUAL_UINT32(2, test_log::count());
    TEST_ASSERT_NOT_NULL(strstr(test_log::line(0), "from async 1"));
    TEST_ASSERT_NOT_NULL(strstr(test_log::line(1), "from async 2"));
}

static void test_loop_log_emits_deferred_lines_first() {
    logger_test::g_foreignTask = true;
    logger.log(LogModule::WebServer, "early\n");
    logger_test::g_foreignTask = false;
    logger.log(LogModule::Core, "late\n");
    TEST_ASSERT_EQUAL_UINT32(2, test_log::count());
    TEST_ASSERT_NOT_NULL(strstr(test_log::line(0), "early"));
    TEST_ASSERT_NOT_NULL(strstr(test_log::line(1), "late"));
}

static void test_ring_overflow_is_counted_and_reported() {
    const uint32_t droppedBefore = logger.deferredDropped();
    logger_test::g_foreignTask = true;
    for (size_t i = 0; i < Logging::DEFERRED_SLOTS + 3; i++) {
        logger.log(LogModule::WebServer, "line %u\n", (unsigned)i);
    }
    logger_test::g_foreignTask = false;
    TEST_ASSERT_EQUAL_UINT32(droppedBefore + 3, logger.deferredDropped());
    logger.flushDeferred();
    TEST_ASSERT_EQUAL_UINT32(Logging::DEFERRED_SLOTS + 1, test_log::count());
    TEST_ASSERT_NOT_NULL(strstr(test_log::last(), "[Logger] 3 deferred lines dropped"));

    test_log::clear();
    logger.flushDeferred();
    TEST_ASSERT_EQUAL_UINT32(0, test_log::count());
}

static void test_flush_from_foreign_task_is_noop() {
    logger_test::g_foreignTask = true;
    logger.log(LogModule::WebServer, "pending\n");
    logger.flushDeferred();
    TEST_ASSERT_EQUAL_UINT32(0, test_log::count());
    logger_test::g_foreignTask = false;
    logger.flushDeferred();
    TEST_ASSERT_EQUAL_UINT32(1, test_log::count());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_line_has_millis_and_module);
    RUN_TEST(test_every_module_has_unique_name);
    RUN_TEST(test_at_channel_keeps_ui_filter_token);
    RUN_TEST(test_truncated_line_ends_with_newline);
    RUN_TEST(test_format_line_helper);
    RUN_TEST(test_foreign_task_lines_are_deferred_until_flush);
    RUN_TEST(test_loop_log_emits_deferred_lines_first);
    RUN_TEST(test_ring_overflow_is_counted_and_reported);
    RUN_TEST(test_flush_from_foreign_task_is_noop);
    return UNITY_END();
}
