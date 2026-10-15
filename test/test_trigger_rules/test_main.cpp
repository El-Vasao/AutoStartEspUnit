#include <unity.h>

#include "core/TriggerRules.h"

using namespace trigger_rules;

void setUp() {}
void tearDown() {}

static void test_compare_from_string() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Compare::Above, (uint8_t)compareFromString("above"));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Compare::Below, (uint8_t)compareFromString("below"));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Compare::Invalid, (uint8_t)compareFromString("Above"));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Compare::Invalid, (uint8_t)compareFromString(nullptr));
}

static void test_edge_latch_fires_once_per_activation() {
    EdgeLatch e;
    TEST_ASSERT_FALSE(e.update(false));
    TEST_ASSERT_TRUE(e.update(true));
    TEST_ASSERT_FALSE(e.update(true));
    TEST_ASSERT_FALSE(e.update(false));
    TEST_ASSERT_TRUE(e.update(true));
}

static void test_threshold_below_with_hysteresis() {
    ThresholdLatch t;
    // Cold start: below -10 fires once.
    TEST_ASSERT_FALSE(t.update(-9.0f, -10.0f, Compare::Below, 0.5f));
    TEST_ASSERT_TRUE(t.update(-10.1f, -10.0f, Compare::Below, 0.5f));
    // Sensor noise around the threshold does not re-fire.
    TEST_ASSERT_FALSE(t.update(-9.9f, -10.0f, Compare::Below, 0.5f));
    TEST_ASSERT_FALSE(t.update(-10.1f, -10.0f, Compare::Below, 0.5f));
    TEST_ASSERT_FALSE(t.update(-9.6f, -10.0f, Compare::Below, 0.5f));
    // Warmed past threshold + hysteresis: re-armed.
    TEST_ASSERT_FALSE(t.update(-9.5f, -10.0f, Compare::Below, 0.5f));
    TEST_ASSERT_TRUE(t.update(-10.2f, -10.0f, Compare::Below, 0.5f));
}

static void test_threshold_above_with_hysteresis() {
    ThresholdLatch t;
    TEST_ASSERT_TRUE(t.update(80.5f, 80.0f, Compare::Above, 1.0f));
    TEST_ASSERT_FALSE(t.update(79.5f, 80.0f, Compare::Above, 1.0f));
    TEST_ASSERT_FALSE(t.update(80.5f, 80.0f, Compare::Above, 1.0f));
    TEST_ASSERT_FALSE(t.update(79.0f, 80.0f, Compare::Above, 1.0f));
    TEST_ASSERT_TRUE(t.update(80.1f, 80.0f, Compare::Above, 1.0f));
}

static void test_threshold_zero_hysteresis_is_plain_edge() {
    ThresholdLatch t;
    TEST_ASSERT_TRUE(t.update(1.0f, 0.0f, Compare::Above, 0.0f));
    TEST_ASSERT_FALSE(t.update(0.0f, 0.0f, Compare::Above, 0.0f));
    TEST_ASSERT_TRUE(t.update(0.5f, 0.0f, Compare::Above, 0.0f));
}

static void test_threshold_invalid_compare_never_fires() {
    ThresholdLatch t;
    TEST_ASSERT_FALSE(t.update(100.0f, 0.0f, Compare::Invalid, 0.5f));
    TEST_ASSERT_FALSE(t.latched);
}

static void test_weekday_bits() {
    TEST_ASSERT_EQUAL_HEX8(0x40, weekdayBit(0));  // Sunday
    TEST_ASSERT_EQUAL_HEX8(0x01, weekdayBit(1));  // Monday
    TEST_ASSERT_EQUAL_HEX8(0x20, weekdayBit(6));  // Saturday
    TEST_ASSERT_EQUAL_HEX8(0x00, weekdayBit(7));
    TEST_ASSERT_EQUAL_HEX8(0x00, weekdayBit(-1));
}

static void test_schedule_due() {
    // Weekdays only (Mon..Fri = 0x1F), 07:30.
    TEST_ASSERT_TRUE(scheduleDue(7, 30, 0x1F, 7, 30, 1));
    TEST_ASSERT_FALSE(scheduleDue(7, 30, 0x1F, 7, 30, 0));   // Sunday
    TEST_ASSERT_FALSE(scheduleDue(7, 30, 0x1F, 7, 31, 1));
    TEST_ASSERT_TRUE(scheduleDue(7, 30, 0x00, 7, 30, 0));    // 0 = every day
    TEST_ASSERT_FALSE(scheduleDue(24, 0, 0x7F, 0, 0, 1));    // invalid hour
}

static void test_schedule_fires_once_per_day_after_success() {
    const uint32_t today = 20261014;
    TEST_ASSERT_TRUE(scheduleShouldFire(true, today, 0));
    // Caller records today only after a successful start; a rejected start retries.
    TEST_ASSERT_TRUE(scheduleShouldFire(true, today, 20261013));
    TEST_ASSERT_FALSE(scheduleShouldFire(true, today, today));
    TEST_ASSERT_FALSE(scheduleShouldFire(false, today, 0));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_compare_from_string);
    RUN_TEST(test_edge_latch_fires_once_per_activation);
    RUN_TEST(test_threshold_below_with_hysteresis);
    RUN_TEST(test_threshold_above_with_hysteresis);
    RUN_TEST(test_threshold_zero_hysteresis_is_plain_edge);
    RUN_TEST(test_threshold_invalid_compare_never_fires);
    RUN_TEST(test_weekday_bits);
    RUN_TEST(test_schedule_due);
    RUN_TEST(test_schedule_fires_once_per_day_after_success);
    return UNITY_END();
}
