#include <unity.h>

#include <string.h>

#include "core/PostedBodyBuffer.h"

void setUp() {}
void tearDown() {}

static void test_arm_rejects_empty_oversize_and_second_arm() {
    PostedBodyBuffer buf;
    TEST_ASSERT_FALSE(buf.arm(0));
    TEST_ASSERT_FALSE(buf.arm(Limits::CONFIG_JSON_SIZE + 1));
    TEST_ASSERT_TRUE(buf.arm(4));
    TEST_ASSERT_FALSE(buf.arm(4));
    TEST_ASSERT_FALSE(buf.complete());
}

static void test_append_rejects_bad_index_null_and_overflow() {
    PostedBodyBuffer buf;
    TEST_ASSERT_TRUE(buf.arm(4));
    const uint8_t bytes[] = {1, 2, 3, 4, 5};
    TEST_ASSERT_FALSE(buf.append(1, bytes, 1));
    TEST_ASSERT_FALSE(buf.append(0, nullptr, 1));
    TEST_ASSERT_FALSE(buf.append(0, bytes, 0));
    TEST_ASSERT_TRUE(buf.append(0, bytes, 3));
    TEST_ASSERT_FALSE(buf.append(3, bytes, 2));
    TEST_ASSERT_FALSE(buf.complete());
    TEST_ASSERT_TRUE(buf.append(3, bytes + 3, 1));
    TEST_ASSERT_TRUE(buf.complete());
    TEST_ASSERT_EQUAL_UINT8(4, buf.data()[3]);
}

static void test_drop_allows_rearm_and_keeps_bytes_until_overwrite() {
    PostedBodyBuffer buf;
    const uint8_t bytes[] = {'a', 'b'};
    TEST_ASSERT_TRUE(buf.arm(2));
    TEST_ASSERT_TRUE(buf.append(0, bytes, 2));
    TEST_ASSERT_TRUE(buf.complete());
    buf.drop();
    TEST_ASSERT_FALSE(buf.complete());
    TEST_ASSERT_FALSE(buf.append(0, bytes, 1));
    TEST_ASSERT_EQUAL_UINT8('a', buf.data()[0]);
    TEST_ASSERT_TRUE(buf.arm(Limits::CONFIG_JSON_SIZE));
    TEST_ASSERT_FALSE(buf.complete());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_arm_rejects_empty_oversize_and_second_arm);
    RUN_TEST(test_append_rejects_bad_index_null_and_overflow);
    RUN_TEST(test_drop_allows_rearm_and_keeps_bytes_until_overwrite);
    return UNITY_END();
}
