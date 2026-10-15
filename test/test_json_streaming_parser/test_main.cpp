#include <unity.h>

#include <string>

#include "JsonListener.h"
#include "JsonStreamingParser.h"

namespace {
/// Records parser events as a compact trace: `{ k:name v:x [ v:1 ] } $`.
class TraceListener : public JsonListener {
public:
    std::string trace;
    int documents = 0;
    void whitespace(char) override {}
    void startDocument() override {}
    void key(const char* k) override { trace += std::string("k:") + k + " "; }
    void value(const char* v) override { trace += std::string("v:") + v + " "; }
    void endArray() override { trace += "] "; }
    void endObject() override { trace += "} "; }
    void endDocument() override {
        trace += "$";
        documents++;
    }
    void startArray() override { trace += "[ "; }
    void startObject() override { trace += "{ "; }
};

struct Outcome {
    std::string trace;
    bool done;
    bool failed;
};

Outcome run(const char* json) {
    JsonStreamingParser parser;
    TraceListener l;
    parser.setListener(&l);
    for (const char* p = json; *p; ++p) parser.parse(*p);
    return {l.trace, parser.done(), parser.failed()};
}

std::string parseAll(const char* json, int* documents = nullptr) {
    JsonStreamingParser parser;
    TraceListener l;
    parser.setListener(&l);
    for (const char* p = json; *p; ++p) parser.parse(*p);
    if (documents) *documents = l.documents;
    return l.trace;
}
} // namespace

void setUp() {}
void tearDown() {}

static void test_flat_object() {
    int docs = 0;
    TEST_ASSERT_EQUAL_STRING("{ k:a v:1 k:b v:x } $", parseAll("{\"a\":1,\"b\":\"x\"}", &docs).c_str());
    TEST_ASSERT_EQUAL_INT(1, docs);
}

static void test_nested_objects_and_arrays() {
    TEST_ASSERT_EQUAL_STRING("{ k:o { k:p v:2 } k:arr [ v:1 { k:q v:3 } [ ] ] k:z v:0 } $",
                             parseAll("{\"o\":{\"p\":2},\"arr\":[1,{\"q\":3},[]],\"z\":0}").c_str());
}

static void test_whitespace_is_ignored_outside_strings() {
    TEST_ASSERT_EQUAL_STRING("{ k:a v:1 k:s v:x y } $", parseAll("{ \"a\" : 1 ,\n\t\"s\" : \"x y\" }").c_str());
}

static void test_literals() {
    TEST_ASSERT_EQUAL_STRING("[ v:true v:false v:null ] $", parseAll("[true,false,null]").c_str());
}

static void test_numbers_are_passed_as_text() {
    TEST_ASSERT_EQUAL_STRING("[ v:-12.5e3 v:0 v:42 ] $", parseAll("[-12.5e3,0,42]").c_str());
}

static void test_simple_escapes() {
    TEST_ASSERT_EQUAL_STRING("[ v:a\"b\\c/d\ne\tf ] $", parseAll("[\"a\\\"b\\\\c\\/d\\ne\\tf\"]").c_str());
}

static void test_utf8_passes_through() {
    TEST_ASSERT_EQUAL_STRING("{ k:name v:Прогрев } $", parseAll("{\"name\":\"Прогрев\"}").c_str());
}

static void test_unicode_escapes_decode_to_utf8() {
    TEST_ASSERT_EQUAL_STRING("[ v:A ] $", parseAll("[\"\\u0041\"]").c_str());
    TEST_ASSERT_EQUAL_STRING("[ v:Д ] $", parseAll("[\"\\u0414\"]").c_str());
    TEST_ASSERT_EQUAL_STRING("[ v:€ ] $", parseAll("[\"\\u20ac\"]").c_str());
    TEST_ASSERT_EQUAL_STRING("[ v:\xF0\x9F\x98\x80 ] $", parseAll("[\"\\ud83d\\ude00\"]").c_str());  // 😀
}

static void test_invalid_unicode_is_an_error() {
    TEST_ASSERT_TRUE(run("[\"\\u00G1\"]").failed);
    TEST_ASSERT_TRUE(run("[\"\\ude00\"]").failed);        // lone low surrogate
    TEST_ASSERT_TRUE(run("[\"\\ud83d x\"]").failed);      // high surrogate without \u
    TEST_ASSERT_TRUE(run("[\"\\ud83d\\u0041\"]").failed); // high surrogate + non-low
}

static void test_done_and_failed_flags() {
    Outcome ok = run("  {\"a\":[1,2]}  ");
    TEST_ASSERT_TRUE(ok.done);
    TEST_ASSERT_FALSE(ok.failed);

    Outcome truncated = run("{\"a\":[1,2");
    TEST_ASSERT_FALSE(truncated.done);
    TEST_ASSERT_FALSE(truncated.failed);

    TEST_ASSERT_TRUE(run("{\"a\" 1}").failed);       // missing ':'
    TEST_ASSERT_TRUE(run("{\"a\":1 \"b\":2}").failed); // missing ','
    TEST_ASSERT_TRUE(run("{\"a\":tru}").failed);
    TEST_ASSERT_TRUE(run("{\"a\":nul}").failed);
    TEST_ASSERT_TRUE(run("{\"a\":x}").failed);
    TEST_ASSERT_TRUE(run("{\"a\":1}]").failed);       // trailing garbage
    TEST_ASSERT_TRUE(run("x").failed);                // root must be object/array
    TEST_ASSERT_TRUE(run("[\"a\\qb\"]").failed);      // bad escape
    TEST_ASSERT_TRUE(run("[1.2.3]").failed);
    TEST_ASSERT_TRUE(run("[-]").failed);
}

static void test_unescaped_control_char_is_an_error() {
    TEST_ASSERT_TRUE(run("[\"a\x01" "b\"]").failed);
    TEST_ASSERT_TRUE(run("[\"a\x1f" "b\"]").failed);
}

static void test_mismatched_closers_are_errors() {
    TEST_ASSERT_TRUE(run("{\"a\":1]").failed);
    TEST_ASSERT_TRUE(run("[1}").failed);
}

static void test_object_closing_tracks_nesting_correctly() {
    // endObject used to read the wrong stack slot; nested objects inside arrays exercise it.
    Outcome o = run("{\"a\":[{\"b\":{}},{}],\"c\":{\"d\":[[]]}}");
    TEST_ASSERT_TRUE(o.done);
    TEST_ASSERT_EQUAL_STRING("{ k:a [ { k:b { } } { } ] k:c { k:d [ [ ] ] } } $", o.trace.c_str());
}

static void test_depth_limit_is_an_error_not_a_crash() {
    std::string deep(JSON_MAX_DEPTH + 5, '[');
    TEST_ASSERT_TRUE(run(deep.c_str()).failed);
    std::string ok(JSON_MAX_DEPTH, '[');
    ok += std::string(JSON_MAX_DEPTH, ']');
    TEST_ASSERT_TRUE(run(ok.c_str()).done);
}

static void test_long_token_is_truncated() {
    std::string json = "[\"" + std::string(BUFFER_MAX_LENGTH + 100, 'x') + "\"]";
    Outcome o = run(json.c_str());
    TEST_ASSERT_TRUE(o.done);
    const std::string expect = "[ v:" + std::string(BUFFER_MAX_LENGTH - 1, 'x') + " ] $";
    TEST_ASSERT_EQUAL_STRING(expect.c_str(), o.trace.c_str());
}

static void test_bom_is_skipped() {
    TEST_ASSERT_TRUE(run("\xEF\xBB\xBF{\"a\":1}").done);
}

static void test_reset_allows_reuse() {
    JsonStreamingParser parser;
    TraceListener l;
    parser.setListener(&l);
    for (const char* p = "[[1"; *p; ++p) parser.parse(*p);
    parser.reset();
    for (const char* p = "{\"a\":2}"; *p; ++p) parser.parse(*p);
    TEST_ASSERT_TRUE(parser.done());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_flat_object);
    RUN_TEST(test_nested_objects_and_arrays);
    RUN_TEST(test_whitespace_is_ignored_outside_strings);
    RUN_TEST(test_literals);
    RUN_TEST(test_numbers_are_passed_as_text);
    RUN_TEST(test_simple_escapes);
    RUN_TEST(test_utf8_passes_through);
    RUN_TEST(test_unicode_escapes_decode_to_utf8);
    RUN_TEST(test_invalid_unicode_is_an_error);
    RUN_TEST(test_done_and_failed_flags);
    RUN_TEST(test_unescaped_control_char_is_an_error);
    RUN_TEST(test_mismatched_closers_are_errors);
    RUN_TEST(test_object_closing_tracks_nesting_correctly);
    RUN_TEST(test_depth_limit_is_an_error_not_a_crash);
    RUN_TEST(test_long_token_is_truncated);
    RUN_TEST(test_bom_is_skipped);
    RUN_TEST(test_reset_allows_reuse);
    return UNITY_END();
}
