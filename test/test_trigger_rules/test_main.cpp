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
    // Холодный старт: ниже −10 срабатывает один раз.
    TEST_ASSERT_FALSE(t.update(-9.0f, -10.0f, Compare::Below, 0.5f));
    TEST_ASSERT_TRUE(t.update(-10.1f, -10.0f, Compare::Below, 0.5f));
    // Шум датчика у порога не вызывает повтор.
    TEST_ASSERT_FALSE(t.update(-9.9f, -10.0f, Compare::Below, 0.5f));
    TEST_ASSERT_FALSE(t.update(-10.1f, -10.0f, Compare::Below, 0.5f));
    TEST_ASSERT_FALSE(t.update(-9.6f, -10.0f, Compare::Below, 0.5f));
    // Прогрев выше порога + гистерезис: снова взведено.
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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_compare_from_string);
    RUN_TEST(test_edge_latch_fires_once_per_activation);
    RUN_TEST(test_threshold_below_with_hysteresis);
    RUN_TEST(test_threshold_above_with_hysteresis);
    RUN_TEST(test_threshold_zero_hysteresis_is_plain_edge);
    RUN_TEST(test_threshold_invalid_compare_never_fires);
    return UNITY_END();
}
