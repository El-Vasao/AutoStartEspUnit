#include <cstdio>
#include <cstring>
#include <string>
#include <unity.h>

#include "config/internal/ProgramJsonIo.h"
#include "program/CompiledStep.h"

using program_json::IndexRow;

namespace {

struct MemPrint : Print {
    std::string s;
    size_t write(uint8_t c) override {
        s.push_back(static_cast<char>(c));
        return 1;
    }
    using Print::write;
};

File fileOf(const std::string& json) { return File(json.data(), json.size()); }

IndexRow makeRow(uint8_t id, const char* name) {
    IndexRow r{};
    r.id = id;
    snprintf(r.name, sizeof r.name, "%s", name);
    return r;
}

} // namespace

void setUp() {}
void tearDown() {}

static void test_format_program_path_normal_and_truncated() {
    char path[32]{};
    program_json::formatProgramPath(path, sizeof path, 200);
    TEST_ASSERT_EQUAL_STRING("/programs/200.json", path);

    char small[8]{};
    program_json::formatProgramPath(small, sizeof small, 7);
    TEST_ASSERT_EQUAL_STRING("/progra", small);
    TEST_ASSERT_EQUAL_INT('\0', small[sizeof small - 1]);

    program_json::formatProgramPath(nullptr, 16, 1);
    program_json::formatProgramPath(path, 0, 1);
}

static void test_index_empty_array_is_ok() {
    IndexRow rows[2]{};
    size_t count = 99;
    File f = fileOf("[]");
    TEST_ASSERT_TRUE(program_json::parseProgramIndexFile(f, rows, 2, &count));
    TEST_ASSERT_EQUAL_UINT32(0, (unsigned)count);
}

static void test_index_reads_rows_and_skips_nested_junk() {
    IndexRow rows[4]{};
    size_t count = 0;
    File f = fileOf(
        "[{\"id\":1,\"name\":\"First\"},"
        "{\"id\":2,\"name\":\"Second\",\"extra\":{\"a\":[1,{\"b\":2}]}},"
        "{\"id\":3,\"name\":\"A\\\"B\\\\C\"}]");
    TEST_ASSERT_TRUE(program_json::parseProgramIndexFile(f, rows, 4, &count));
    TEST_ASSERT_EQUAL_UINT32(3, (unsigned)count);
    TEST_ASSERT_EQUAL_UINT8(1, rows[0].id);
    TEST_ASSERT_EQUAL_STRING("First", rows[0].name);
    TEST_ASSERT_EQUAL_UINT8(2, rows[1].id);
    TEST_ASSERT_EQUAL_STRING("Second", rows[1].name);
    TEST_ASSERT_EQUAL_UINT8(3, rows[2].id);
    TEST_ASSERT_EQUAL_STRING("A\"B\\C", rows[2].name);
}

static void test_index_too_many_rows_fails() {
    IndexRow rows[2]{};
    size_t count = 77;
    File f = fileOf("[{\"id\":1,\"name\":\"a\"},{\"id\":2,\"name\":\"b\"},{\"id\":3,\"name\":\"c\"}]");
    TEST_ASSERT_FALSE(program_json::parseProgramIndexFile(f, rows, 2, &count));
    TEST_ASSERT_EQUAL_UINT32(0, (unsigned)count);
}

static void test_index_root_must_be_array() {
    IndexRow rows[2]{};
    size_t count = 0;
    File object = fileOf("{\"id\":1,\"name\":\"a\"}");
    TEST_ASSERT_FALSE(program_json::parseProgramIndexFile(object, rows, 2, &count));
    File scalar = fileOf("42");
    TEST_ASSERT_FALSE(program_json::parseProgramIndexFile(scalar, rows, 2, &count));
}

static void test_index_null_rows_with_zero_capacity_discards() {
    size_t count = 99;
    File f = fileOf("[{\"id\":1,\"name\":\"a\"},{\"id\":2,\"name\":\"b\"}]");
    TEST_ASSERT_TRUE(program_json::parseProgramIndexFile(f, nullptr, 0, &count));
    TEST_ASSERT_EQUAL_UINT32(0, (unsigned)count);
}

static void test_index_null_and_unknown_values_are_ignored() {
    IndexRow rows[2]{};
    size_t count = 0;
    File f = fileOf(
        "[{\"id\":null,\"name\":null},"
        "{\"id\":\"bad\",\"name\":\"kept\",\"other\":[1,2]}]");
    TEST_ASSERT_TRUE(program_json::parseProgramIndexFile(f, rows, 2, &count));
    TEST_ASSERT_EQUAL_UINT32(2, (unsigned)count);
    TEST_ASSERT_EQUAL_UINT8(0, rows[0].id);
    TEST_ASSERT_EQUAL_STRING("", rows[0].name);
    TEST_ASSERT_EQUAL_UINT8(0, rows[1].id);
    TEST_ASSERT_EQUAL_STRING("kept", rows[1].name);
}

static void test_header_reads_id_name_and_skips_steps() {
    uint8_t id = 0;
    char name[TextBytes::Programs::NAME]{};
    File f = fileOf(
        "{\"id\":7,\"name\":\"Night\","
        "\"extra\":{\"deep\":[1,{\"x\":2}]},"
        "\"steps\":[{\"action\":\"WAIT\",\"nested\":{\"a\":[1,2]}},"
        "{\"action\":\"RELAY_ON\",\"inner\":{\"b\":3}}]}");
    TEST_ASSERT_TRUE(program_json::parseProgramHeaderFile(f, 7, &id, name, sizeof name));
    TEST_ASSERT_EQUAL_UINT8(7, id);
    TEST_ASSERT_EQUAL_STRING("Night", name);
}

static void test_header_id_mismatch_fails() {
    uint8_t id = 0;
    char name[TextBytes::Programs::NAME]{};
    File f = fileOf("{\"id\":2,\"name\":\"N\",\"steps\":[]}");
    TEST_ASSERT_FALSE(program_json::parseProgramHeaderFile(f, 3, &id, name, sizeof name));
    TEST_ASSERT_EQUAL_UINT8(2, id);
}

static void test_header_null_name_and_missing_id() {
    uint8_t id = 1;
    char name[TextBytes::Programs::NAME]{};
    snprintf(name, sizeof name, "stale");
    File nullName = fileOf("{\"id\":7,\"name\":null,\"steps\":[]}");
    TEST_ASSERT_TRUE(program_json::parseProgramHeaderFile(nullName, 7, &id, name, sizeof name));
    TEST_ASSERT_EQUAL_UINT8(7, id);
    TEST_ASSERT_EQUAL_STRING("", name);

    id = 1;
    File noId = fileOf("{\"name\":\"NoId\"}");
    TEST_ASSERT_TRUE(program_json::parseProgramHeaderFile(noId, 0, &id, name, sizeof name));
    TEST_ASSERT_EQUAL_UINT8(0, id);
    TEST_ASSERT_EQUAL_STRING("NoId", name);
}

static void test_header_steps_object_is_treated_as_junk() {
    uint8_t id = 0;
    char name[TextBytes::Programs::NAME]{};
    File f = fileOf("{\"id\":7,\"name\":\"N\",\"steps\":{\"a\":1},\"tail\":[1,2]}");
    TEST_ASSERT_TRUE(program_json::parseProgramHeaderFile(f, 7, &id, name, sizeof name));
    TEST_ASSERT_EQUAL_UINT8(7, id);
    TEST_ASSERT_EQUAL_STRING("N", name);
}

static void test_compiled_parses_all_fields() {
    CompiledStep steps[4]{};
    uint8_t count = 0;
    char name[TextBytes::Programs::NAME]{};
    File f = fileOf(
        "{\"id\":2,\"name\":\"P\",\"steps\":["
        "{\"step\":1,\"action\":\"RELAY_ON\",\"relay_id\":\"K3\",\"ms\":1500},"
        "{\"action\":\"INPUT_ENABLE\",\"input_id\":\"IN2\","
        "\"input_trigger_id\":7,\"temp_trigger_id\":9,\"message\":\"hi\"}]}");
    TEST_ASSERT_TRUE(program_json::parseProgramCompiledFile(f, 2, steps, 4, &count, name, sizeof name));
    TEST_ASSERT_EQUAL_UINT8(2, count);
    TEST_ASSERT_EQUAL_STRING("P", name);
    TEST_ASSERT_EQUAL((int)ActionId::RELAY_ON, (int)steps[0].action);
    TEST_ASSERT_EQUAL_UINT8(3, steps[0].relay_id);
    TEST_ASSERT_EQUAL_UINT32(1500, steps[0].ms);
    TEST_ASSERT_EQUAL((int)ActionId::INPUT_ENABLE, (int)steps[1].action);
    TEST_ASSERT_EQUAL_UINT8(2, steps[1].input_id);
    TEST_ASSERT_EQUAL_UINT16(7, steps[1].input_trigger_id);
    TEST_ASSERT_EQUAL_UINT16(9, steps[1].temp_trigger_id);
    TEST_ASSERT_EQUAL_STRING("hi", steps[1].message);
}

static void test_compiled_id_mismatch_fails() {
    CompiledStep steps[2]{};
    uint8_t count = 0;
    char name[TextBytes::Programs::NAME]{};
    File f = fileOf("{\"id\":2,\"steps\":[{\"action\":\"WAIT\"}]}");
    TEST_ASSERT_FALSE(program_json::parseProgramCompiledFile(f, 3, steps, 2, &count, name, sizeof name));
}

static void test_compiled_null_args_fail() {
    CompiledStep steps[2]{};
    uint8_t count = 0;
    char name[TextBytes::Programs::NAME]{};
    const char* doc = "{\"id\":1,\"steps\":[{\"action\":\"WAIT\"}]}";

    File a = fileOf(doc);
    TEST_ASSERT_FALSE(program_json::parseProgramCompiledFile(a, 1, nullptr, 2, &count, name, sizeof name));
    File b = fileOf(doc);
    TEST_ASSERT_FALSE(program_json::parseProgramCompiledFile(b, 1, steps, 0, &count, name, sizeof name));
    File c = fileOf(doc);
    TEST_ASSERT_FALSE(program_json::parseProgramCompiledFile(c, 1, steps, 2, nullptr, name, sizeof name));
    File d = fileOf(doc);
    TEST_ASSERT_FALSE(program_json::parseProgramCompiledFile(d, 1, steps, 2, &count, nullptr, sizeof name));
    File e = fileOf(doc);
    TEST_ASSERT_FALSE(program_json::parseProgramCompiledFile(e, 1, steps, 2, &count, name, 0));
}

static void test_compiled_max_steps_overflow_fails() {
    CompiledStep steps[2]{};
    uint8_t count = 0;
    char name[TextBytes::Programs::NAME]{};
    File f = fileOf(
        "{\"id\":1,\"steps\":[{\"action\":\"WAIT\"},{\"action\":\"RELAY_OFF\"}]}");
    TEST_ASSERT_FALSE(program_json::parseProgramCompiledFile(f, 1, steps, 1, &count, name, sizeof name));
}

static void test_compiled_unknown_or_missing_action_is_unknown() {
    CompiledStep steps[2]{};
    uint8_t count = 0;
    char name[TextBytes::Programs::NAME]{};
    File f = fileOf(
        "{\"id\":1,\"steps\":[{\"action\":\"NOPE\"},{\"ms\":5}]}");
    TEST_ASSERT_TRUE(program_json::parseProgramCompiledFile(f, 1, steps, 2, &count, name, sizeof name));
    TEST_ASSERT_EQUAL_UINT8(2, count);
    TEST_ASSERT_EQUAL((int)ActionId::UNKNOWN, (int)steps[0].action);
    TEST_ASSERT_EQUAL((int)ActionId::UNKNOWN, (int)steps[1].action);
}

static void test_compiled_invalid_silk_fails() {
    CompiledStep steps[2]{};
    uint8_t count = 0;
    char name[TextBytes::Programs::NAME]{};
    File f = fileOf("{\"id\":1,\"steps\":[{\"action\":\"RELAY_ON\",\"relay_id\":\"K9\"}]}");
    TEST_ASSERT_FALSE(program_json::parseProgramCompiledFile(f, 1, steps, 2, &count, name, sizeof name));
}

static void test_program_object_requires_id_and_nonempty_steps() {
    Program p;
    File emptySteps = fileOf("{\"id\":1,\"name\":\"p\",\"steps\":[]}");
    TEST_ASSERT_FALSE(program_json::parseProgramObjectFile(emptySteps, p));
    File noSteps = fileOf("{\"id\":1,\"name\":\"p\"}");
    TEST_ASSERT_FALSE(program_json::parseProgramObjectFile(noSteps, p));
    File zeroId = fileOf("{\"id\":0,\"name\":\"p\",\"steps\":[{\"action\":\"WAIT\"}]}");
    TEST_ASSERT_FALSE(program_json::parseProgramObjectFile(zeroId, p));
}

static void test_program_object_skips_junk_inside_step() {
    Program p;
    File f = fileOf(
        "{\"id\":1,\"name\":\"p\",\"steps\":["
        "{\"action\":\"WAIT\",\"ms\":5,\"extra\":{\"a\":[1,2]}}]}");
    TEST_ASSERT_TRUE(program_json::parseProgramObjectFile(f, p));
    TEST_ASSERT_EQUAL_UINT8(1, p.step_count);
    TEST_ASSERT_EQUAL((int)ActionId::WAIT, (int)actionIdFromString(p.steps[0].action));
    TEST_ASSERT_EQUAL_UINT32(5, p.steps[0].ms);
}

static void test_compiled_skips_nested_and_root_junk() {
    CompiledStep steps[2]{};
    uint8_t count = 0;
    char name[TextBytes::Programs::NAME]{};
    File f = fileOf(
        "{\"id\":2,\"steps\":[{\"action\":\"WAIT\",\"extra\":{\"a\":1}}],"
        "\"rootObj\":{\"x\":1},\"rootArr\":[1,2]}");
    TEST_ASSERT_TRUE(program_json::parseProgramCompiledFile(f, 2, steps, 2, &count, name, sizeof name));
    TEST_ASSERT_EQUAL_UINT8(1, count);
    TEST_ASSERT_EQUAL((int)ActionId::WAIT, (int)steps[0].action);
}

static void test_whitespace_and_root_junk_are_ignored() {
    IndexRow rows[2]{};
    size_t count = 0;
    File idx = fileOf(" \n [ { \"id\" : 1 , \"name\" : \"a\" } ] \t");
    TEST_ASSERT_TRUE(program_json::parseProgramIndexFile(idx, rows, 2, &count));
    TEST_ASSERT_EQUAL_UINT32(1, (unsigned)count);
    TEST_ASSERT_EQUAL_STRING("a", rows[0].name);

    uint8_t id = 0;
    char name[TextBytes::Programs::NAME]{};
    File hdr = fileOf(" { \"id\" : 7 , \"name\" : \"N\" , \"steps\" : [ ] , \"extra\" : { \"x\" : 1 } } ");
    TEST_ASSERT_TRUE(program_json::parseProgramHeaderFile(hdr, 7, &id, name, sizeof name));
    TEST_ASSERT_EQUAL_UINT8(7, id);
    TEST_ASSERT_EQUAL_STRING("N", name);

    Program p;
    File progObj = fileOf(
        " { \"id\" : 1 , \"name\" : \"p\" , \"steps\" : [ { \"action\" : \"WAIT\" } ] ,"
        " \"extra\" : { \"x\" : [ 1 ] } } ");
    TEST_ASSERT_TRUE(program_json::parseProgramObjectFile(progObj, p));
    TEST_ASSERT_EQUAL_UINT8(1, p.step_count);

    Program p2;
    File progArr = fileOf("{\"id\":1,\"steps\":[{\"action\":\"WAIT\"}],\"extra\":[1,2]}");
    TEST_ASSERT_TRUE(program_json::parseProgramObjectFile(progArr, p2));
    TEST_ASSERT_EQUAL_UINT8(1, p2.step_count);
}

static void test_emit_program_zero_steps_and_count() {
    Program p;
    MemPrint out;
    const size_t n = program_json::emitProgramToPrint(p, out);
    TEST_ASSERT_EQUAL_STRING("{\"id\":0,\"name\":\"\",\"steps\":[]}", out.s.c_str());
    TEST_ASSERT_EQUAL_UINT32((unsigned)out.s.size(), (unsigned)n);
}

static void test_emit_program_optional_fields_and_escaping() {
    Program p;
    p.id = 3;
    snprintf(p.name, sizeof p.name, "A\"B");
    p.step_count = 5;

    p.steps[0].step = 1;
    snprintf(p.steps[0].action, sizeof p.steps[0].action, "RELAY_ON");
    p.steps[0].relay_id = 3; // K3

    p.steps[1].step = 2;
    snprintf(p.steps[1].action, sizeof p.steps[1].action, "WAIT");
    p.steps[1].relay_id = 2; // не RELAY_* — relay_id не должен попасть

    p.steps[2].step = 3;
    snprintf(p.steps[2].action, sizeof p.steps[2].action, "SMS_OWNER");
    snprintf(p.steps[2].message, sizeof p.steps[2].message, "hi \"x\"");

    p.steps[3].step = 4;
    snprintf(p.steps[3].action, sizeof p.steps[3].action, "ENGINE_START");
    p.steps[3].relay_id = 2; // не RELAY_* — не должен попасть

    p.steps[4].step = 5;
    snprintf(p.steps[4].action, sizeof p.steps[4].action, "RELAY_OFF");
    p.steps[4].relay_id = 0; // нулевой silk не пишется

    MemPrint out;
    const size_t n = program_json::emitProgramToPrint(p, out);
    TEST_ASSERT_EQUAL_UINT32((unsigned)out.s.size(), (unsigned)n);
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"id\":3"));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"name\":\"A\\\"B\""));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "{\"step\":1,\"action\":\"RELAY_ON\",\"relay_id\":\"K3\"}"));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "{\"step\":2,\"action\":\"WAIT\"}"));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"message\":\"hi \\\"x\\\"\""));
    TEST_ASSERT_NULL(strstr(out.s.c_str(), "ENGINE_START\",\"relay_id\""));
    TEST_ASSERT_NULL(strstr(out.s.c_str(), "RELAY_OFF\",\"relay_id\""));
}

static void test_emit_program_relay_prefix_requires_underscore() {
    Program p;
    p.id = 1;
    p.step_count = 1;
    p.steps[0].step = 1;
    snprintf(p.steps[0].action, sizeof p.steps[0].action, "RELAY");
    p.steps[0].relay_id = 3;

    MemPrint out;
    program_json::emitProgramToPrint(p, out);
    TEST_ASSERT_NULL(strstr(out.s.c_str(), "relay_id"));
}

static void test_emit_index_empty_and_rows() {
    MemPrint empty;
    const size_t n0 = program_json::emitProgramIndexArrayPrint(nullptr, 0, empty);
    TEST_ASSERT_EQUAL_STRING("[]", empty.s.c_str());
    TEST_ASSERT_EQUAL_UINT32(2, (unsigned)n0);

    IndexRow rows[2] = {makeRow(1, "A\"B"), makeRow(2, "C\\D")};
    MemPrint out;
    const size_t n = program_json::emitProgramIndexArrayPrint(rows, 2, out);
    TEST_ASSERT_EQUAL_UINT32((unsigned)out.s.size(), (unsigned)n);
    TEST_ASSERT_EQUAL_STRING("[{\"id\":1,\"name\":\"A\\\"B\"},{\"id\":2,\"name\":\"C\\\\D\"}]", out.s.c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_format_program_path_normal_and_truncated);
    RUN_TEST(test_index_empty_array_is_ok);
    RUN_TEST(test_index_reads_rows_and_skips_nested_junk);
    RUN_TEST(test_index_too_many_rows_fails);
    RUN_TEST(test_index_root_must_be_array);
    RUN_TEST(test_index_null_rows_with_zero_capacity_discards);
    RUN_TEST(test_index_null_and_unknown_values_are_ignored);
    RUN_TEST(test_header_reads_id_name_and_skips_steps);
    RUN_TEST(test_header_id_mismatch_fails);
    RUN_TEST(test_header_null_name_and_missing_id);
    RUN_TEST(test_header_steps_object_is_treated_as_junk);
    RUN_TEST(test_compiled_parses_all_fields);
    RUN_TEST(test_compiled_id_mismatch_fails);
    RUN_TEST(test_compiled_null_args_fail);
    RUN_TEST(test_compiled_max_steps_overflow_fails);
    RUN_TEST(test_compiled_unknown_or_missing_action_is_unknown);
    RUN_TEST(test_compiled_invalid_silk_fails);
    RUN_TEST(test_program_object_requires_id_and_nonempty_steps);
    RUN_TEST(test_program_object_skips_junk_inside_step);
    RUN_TEST(test_compiled_skips_nested_and_root_junk);
    RUN_TEST(test_whitespace_and_root_junk_are_ignored);
    RUN_TEST(test_emit_program_zero_steps_and_count);
    RUN_TEST(test_emit_program_optional_fields_and_escaping);
    RUN_TEST(test_emit_program_relay_prefix_requires_underscore);
    RUN_TEST(test_emit_index_empty_and_rows);
    return UNITY_END();
}
