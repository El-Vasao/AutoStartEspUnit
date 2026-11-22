#include <unity.h>

#include "common/Constants.h"
#include "common/Pins.h"
#include "config/Config.h"
#include "io/DigitalInputs.h"

static uint8_t pin0() { return Pin::INPUT_PINS[0]; }

static void digitalActiveHigh() {
    InputConfig& in = config.testBase().inputs[0];
    in = InputConfig{};
    in.id = 1;
    in.enabled = true;
    in.type = InputType::DIGITAL;
    in.active_state = true;
}

void setUp() {
    test_clock::set(100000);
    test_gpio::reset();
    digitalActiveHigh();
}

void tearDown() {}

static void test_debounce_accepts_level_only_after_delay() {
    DigitalInputs inputs;
    inputs.begin();
    TEST_ASSERT_FALSE(inputs.getState(0));

    digitalWrite(pin0(), HIGH);
    inputs.update();
    TEST_ASSERT_FALSE(inputs.getState(0));

    test_clock::advance(Timing::DEBOUNCE_DELAY_MS - 1);
    inputs.update();
    TEST_ASSERT_FALSE(inputs.getState(0));

    test_clock::advance(1);
    inputs.update();
    TEST_ASSERT_TRUE(inputs.getState(0));
    TEST_ASSERT_TRUE(inputs.wasPressed(0));
}

static void test_glitch_shorter_than_debounce_is_ignored() {
    DigitalInputs inputs;
    inputs.begin();

    digitalWrite(pin0(), HIGH);
    inputs.update();
    test_clock::advance(10);
    digitalWrite(pin0(), LOW);
    inputs.update();
    test_clock::advance(Timing::DEBOUNCE_DELAY_MS);
    inputs.update();

    TEST_ASSERT_FALSE(inputs.getState(0));
    TEST_ASSERT_FALSE(inputs.wasPressed(0));
}

static void test_debounce_survives_millis_wrap() {
    test_clock::set(0xFFFFFFF0UL);
    DigitalInputs inputs;
    inputs.begin();
    digitalWrite(pin0(), HIGH);
    inputs.update();
    test_clock::advance(Timing::DEBOUNCE_DELAY_MS);
    inputs.update();
    TEST_ASSERT_TRUE(millis() < 0xFFFFFFF0UL);
    TEST_ASSERT_TRUE(inputs.getState(0));
}

static void test_disabled_input_stays_inactive() {
    config.testBase().inputs[0].enabled = false;
    digitalWrite(pin0(), HIGH);
    DigitalInputs inputs;
    inputs.begin();
    inputs.update();
    TEST_ASSERT_FALSE(inputs.isRuntimeEnabled(0));
    TEST_ASSERT_FALSE(inputs.getState(0));

    test_clock::advance(1000);
    inputs.update();
    TEST_ASSERT_FALSE(inputs.getState(0));
}

static void test_runtime_disable_forces_inactive_while_pin_is_active() {
    DigitalInputs inputs;
    inputs.begin();
    digitalWrite(pin0(), HIGH);
    inputs.update();
    test_clock::advance(Timing::DEBOUNCE_DELAY_MS);
    inputs.update();
    TEST_ASSERT_TRUE(inputs.getState(0));

    inputs.setRuntimeEnabled(0, false);
    TEST_ASSERT_FALSE(inputs.getState(0));
    inputs.update();
    TEST_ASSERT_FALSE(inputs.getState(0));
    TEST_ASSERT_FALSE(inputs.wasPressed(0));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_debounce_accepts_level_only_after_delay);
    RUN_TEST(test_glitch_shorter_than_debounce_is_ignored);
    RUN_TEST(test_debounce_survives_millis_wrap);
    RUN_TEST(test_disabled_input_stays_inactive);
    RUN_TEST(test_runtime_disable_forces_inactive_while_pin_is_active);
    return UNITY_END();
}
