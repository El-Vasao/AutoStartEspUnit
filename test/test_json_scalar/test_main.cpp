#include <unity.h>

#include "config/internal/JsonScalar.h"

using namespace json_scalar;

void setUp() {}
void tearDown() {}

static void test_bool_tokens() {
    TEST_ASSERT_TRUE(parseBool("true", false));
    TEST_ASSERT_TRUE(parseBool("1", false));
    TEST_ASSERT_FALSE(parseBool("false", true));
    TEST_ASSERT_FALSE(parseBool("0", true));
    TEST_ASSERT_TRUE(parseBool("yes", true));
    TEST_ASSERT_FALSE(parseBool(nullptr, false));
    TEST_ASSERT_TRUE(parseBool("", true));
}

static void test_u32_accepts_only_whole_unsigned_decimals() {
    TEST_ASSERT_EQUAL_UINT32(0, parseU32("0", 7));
    TEST_ASSERT_EQUAL_UINT32(4294967295UL, parseU32("4294967295", 7));
    TEST_ASSERT_EQUAL_UINT32(7, parseU32("4294967296", 7));  // overflow
    TEST_ASSERT_EQUAL_UINT32(7, parseU32("-1", 7));
    TEST_ASSERT_EQUAL_UINT32(7, parseU32(" 5", 7));
    TEST_ASSERT_EQUAL_UINT32(7, parseU32("5x", 7));
    TEST_ASSERT_EQUAL_UINT32(7, parseU32("1.5", 7));
    TEST_ASSERT_EQUAL_UINT32(7, parseU32("", 7));
    TEST_ASSERT_EQUAL_UINT32(7, parseU32(nullptr, 7));
}

static void test_u16_and_u8_saturate_but_reject_negative() {
    TEST_ASSERT_EQUAL_UINT16(65535, parseU16("70000", 1));
    TEST_ASSERT_EQUAL_UINT16(1, parseU16("-1", 1));
    TEST_ASSERT_EQUAL_UINT16(1234, parseU16("1234", 1));
    TEST_ASSERT_EQUAL_UINT8(255, parseU8("300", 1));
    TEST_ASSERT_EQUAL_UINT8(1, parseU8("-3", 1));
    TEST_ASSERT_EQUAL_UINT8(42, parseU8("42", 1));
    TEST_ASSERT_EQUAL_UINT8(1, parseU8("99999999999999999999", 1));
}

static void test_i16() {
    TEST_ASSERT_EQUAL_INT16(-12, parseI16("-12", 0));
    TEST_ASSERT_EQUAL_INT16(32767, parseI16("40000", 0));
    TEST_ASSERT_EQUAL_INT16(-32768, parseI16("-40000", 0));
    TEST_ASSERT_EQUAL_INT16(5, parseI16("--1", 5));
    TEST_ASSERT_EQUAL_INT16(5, parseI16("-", 5));
    TEST_ASSERT_EQUAL_INT16(5, parseI16("3a", 5));
}

static void test_float() {
    TEST_ASSERT_EQUAL_FLOAT(13.2f, parseF("13.2", 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(-0.5f, parseF("-0.5", 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, parseF("1e3", 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, parseF("abc", 2.0f));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, parseF("12.5x", 2.0f));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, parseF("nan", 2.0f));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, parseF("inf", 2.0f));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, parseF("", 2.0f));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_bool_tokens);
    RUN_TEST(test_u32_accepts_only_whole_unsigned_decimals);
    RUN_TEST(test_u16_and_u8_saturate_but_reject_negative);
    RUN_TEST(test_i16);
    RUN_TEST(test_float);
    return UNITY_END();
}
