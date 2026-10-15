#include <unity.h>

#include <string>

#include "common/JsonEscape.h"

namespace {
struct StringPrint : Print {
    std::string s;
    size_t write(uint8_t c) override {
        s.push_back(static_cast<char>(c));
        return 1;
    }
    using Print::write;
};

std::string esc(const char* in) {
    char out[128];
    json_escape::escapeTo(out, sizeof(out), in);
    return out;
}
} // namespace

void setUp() {}
void tearDown() {}

static void test_plain_text_passes_through() {
    TEST_ASSERT_EQUAL_STRING("hello world", esc("hello world").c_str());
}

static void test_quotes_and_backslash() {
    TEST_ASSERT_EQUAL_STRING("say \\\"hi\\\" \\\\ ok", esc("say \"hi\" \\ ok").c_str());
}

static void test_short_control_escapes() {
    TEST_ASSERT_EQUAL_STRING("a\\nb\\rc\\td\\be\\ff", esc("a\nb\rc\td\be\ff").c_str());
}

static void test_other_controls_become_unicode_escapes() {
    TEST_ASSERT_EQUAL_STRING("\\u0001\\u001f", esc("\x01\x1f").c_str());
}

static void test_utf8_is_not_touched() {
    TEST_ASSERT_EQUAL_STRING("Привет — ok", esc("Привет — ok").c_str());
}

static void test_escape_to_reports_full_length_and_truncates_on_boundary() {
    char out[6];
    const size_t need = json_escape::escapeTo(out, sizeof(out), "ab\"cd");
    TEST_ASSERT_EQUAL_UINT32(6, need);           // a b \ " c d
    TEST_ASSERT_EQUAL_STRING("ab\\\"c", out);    // 5 chars fit, no split escape
    char tiny[3];
    json_escape::escapeTo(tiny, sizeof(tiny), "\"x");
    TEST_ASSERT_EQUAL_STRING("\\\"", tiny);
    char none[1];
    json_escape::escapeTo(none, sizeof(none), "abc");
    TEST_ASSERT_EQUAL_STRING("", none);
}

static void test_null_input() {
    TEST_ASSERT_EQUAL_STRING("", esc(nullptr).c_str());
    StringPrint p;
    TEST_ASSERT_EQUAL_UINT32(0, json_escape::writeEscaped(p, nullptr));
    json_escape::writeQuoted(p, nullptr);
    TEST_ASSERT_EQUAL_STRING("\"\"", p.s.c_str());
}

static void test_print_variants() {
    StringPrint p;
    const size_t n = json_escape::writeQuoted(p, "a\"b");
    TEST_ASSERT_EQUAL_STRING("\"a\\\"b\"", p.s.c_str());
    TEST_ASSERT_EQUAL_UINT32(p.s.size(), n);
    StringPrint q;
    json_escape::writeEscaped(q, "x\ny");
    TEST_ASSERT_EQUAL_STRING("x\\ny", q.s.c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_plain_text_passes_through);
    RUN_TEST(test_quotes_and_backslash);
    RUN_TEST(test_short_control_escapes);
    RUN_TEST(test_other_controls_become_unicode_escapes);
    RUN_TEST(test_utf8_is_not_touched);
    RUN_TEST(test_escape_to_reports_full_length_and_truncates_on_boundary);
    RUN_TEST(test_null_input);
    RUN_TEST(test_print_variants);
    return UNITY_END();
}
