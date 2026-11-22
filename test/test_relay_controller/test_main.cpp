#include <unity.h>

#include "common/Constants.h"
#include "common/Pins.h"
#include "common/TimeMath.h"
#include "config/Config.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "esp_timer.h"
#include "io/RelayController.h"

static uint8_t starterPin() { return Pin::RELAY_PINS[2]; }

static void boot(RelayController& relays) {
    relays.begin();
    TEST_ASSERT_EQUAL_INT8(2, relays.starterIndex());
}

void setUp() {
    test_clock::set(100000);
    test_gpio::reset();
    test_esp_timer::reset();
    config.testBase().vehicle = VehicleConfig{};
    core.getErrorManager().clear();
}

void tearDown() {}

static void test_timeout_forces_off_sets_error_and_locks_starter() {
    RelayController relays;
    boot(relays);
    TEST_ASSERT_TRUE(relays.on(2));
    TEST_ASSERT_EQUAL(HIGH, digitalRead(starterPin()));

    test_clock::advance(secToMs(StarterLimits::MAX_TIME_DEFAULT_SEC));
    relays.update();
    TEST_ASSERT_TRUE(relays.getState(2));

    test_clock::advance(1);
    relays.update();
    TEST_ASSERT_FALSE(relays.getState(2));
    TEST_ASSERT_EQUAL(LOW, digitalRead(starterPin()));
    TEST_ASSERT_TRUE(core.getErrorManager().isActive(ErrorCode::RELAY_TIMEOUT));
    TEST_ASSERT_TRUE(relays.isStarterLockedOut());
    TEST_ASSERT_FALSE(relays.on(2));
}

static void test_repeat_on_does_not_extend_window() {
    RelayController relays;
    boot(relays);
    TEST_ASSERT_TRUE(relays.on(2));
    test_clock::advance(4000);
    TEST_ASSERT_TRUE(relays.on(2));
    test_clock::advance(1001);
    relays.update();
    TEST_ASSERT_FALSE(relays.getState(2));
    TEST_ASSERT_TRUE(relays.isStarterLockedOut());
}

static void test_non_starter_relay_has_no_max_on_time() {
    RelayController relays;
    boot(relays);
    TEST_ASSERT_TRUE(relays.on(0));
    test_clock::advance(60000);
    relays.update();
    TEST_ASSERT_TRUE(relays.getState(0));
    TEST_ASSERT_FALSE(core.getErrorManager().isActive(ErrorCode::RELAY_TIMEOUT));
}

static void test_lockout_uses_longer_of_pause_and_minimum() {
    config.testBase().vehicle.starter_retry_pause_sec = 8;
    RelayController relays;
    boot(relays);
    TEST_ASSERT_TRUE(relays.on(2));
    test_clock::advance(secToMs(StarterLimits::MAX_TIME_DEFAULT_SEC) + 1);
    relays.update();
    TEST_ASSERT_TRUE(relays.isStarterLockedOut());

    test_clock::advance(7999);
    relays.update();
    TEST_ASSERT_FALSE(relays.on(2));

    test_clock::advance(1);
    relays.update();
    TEST_ASSERT_FALSE(relays.isStarterLockedOut());
    TEST_ASSERT_TRUE(relays.on(2));
}

static void test_lockout_not_shorter_than_minimum() {
    config.testBase().vehicle.starter_retry_pause_sec = 1;
    RelayController relays;
    boot(relays);
    TEST_ASSERT_TRUE(relays.on(2));
    test_clock::advance(secToMs(StarterLimits::MAX_TIME_DEFAULT_SEC) + 1);
    relays.update();

    test_clock::advance(StarterLimits::LOCKOUT_MIN_MS - 1);
    relays.update();
    TEST_ASSERT_TRUE(relays.isStarterLockedOut());

    test_clock::advance(1);
    relays.update();
    TEST_ASSERT_FALSE(relays.isStarterLockedOut());
}

static void test_timeout_across_millis_wrap() {
    test_clock::set(0xFFFFF000UL);
    RelayController relays;
    boot(relays);
    TEST_ASSERT_TRUE(relays.on(2));
    const unsigned long started = millis();
    test_clock::advance(secToMs(StarterLimits::MAX_TIME_DEFAULT_SEC) + 1);
    TEST_ASSERT_TRUE(millis() < started);
    relays.update();
    TEST_ASSERT_FALSE(relays.getState(2));
    TEST_ASSERT_TRUE(core.getErrorManager().isActive(ErrorCode::RELAY_TIMEOUT));
    TEST_ASSERT_TRUE(relays.isStarterLockedOut());
}

static void test_backstop_cuts_pin_before_update_clears_state() {
    RelayController relays;
    boot(relays);
    TEST_ASSERT_TRUE(relays.on(2));
    const uint64_t expectUs =
        (uint64_t)(secToMs(StarterLimits::MAX_TIME_DEFAULT_SEC) + StarterLimits::HW_BACKSTOP_MARGIN_MS) * 1000ULL;
    TEST_ASSERT_EQUAL_UINT32((uint32_t)expectUs, (uint32_t)test_esp_timer::timeoutUs());
    TEST_ASSERT_TRUE(test_esp_timer::armed());
    TEST_ASSERT_EQUAL(HIGH, digitalRead(starterPin()));

    test_esp_timer::fire();
    TEST_ASSERT_EQUAL(LOW, digitalRead(starterPin()));
    TEST_ASSERT_TRUE(relays.getState(2));
    TEST_ASSERT_TRUE(relays.isStarterLockedOut());
    TEST_ASSERT_FALSE(relays.on(2));

    relays.update();
    TEST_ASSERT_FALSE(relays.getState(2));
    TEST_ASSERT_EQUAL(LOW, digitalRead(starterPin()));
    TEST_ASSERT_TRUE(core.getErrorManager().isActive(ErrorCode::RELAY_TIMEOUT));
    TEST_ASSERT_TRUE(relays.isStarterLockedOut());
    TEST_ASSERT_FALSE(test_esp_timer::armed());
}

static void test_off_disarms_backstop_and_out_of_range_is_rejected() {
    RelayController relays;
    boot(relays);
    TEST_ASSERT_TRUE(relays.on(2));
    TEST_ASSERT_TRUE(test_esp_timer::armed());
    relays.off(2);
    TEST_ASSERT_FALSE(relays.getState(2));
    TEST_ASSERT_FALSE(test_esp_timer::armed());
    TEST_ASSERT_FALSE(relays.on(HardwareLimits::RELAYS));
    relays.allOff();
    TEST_ASSERT_EQUAL_UINT8(0, relays.getMask());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_timeout_forces_off_sets_error_and_locks_starter);
    RUN_TEST(test_repeat_on_does_not_extend_window);
    RUN_TEST(test_non_starter_relay_has_no_max_on_time);
    RUN_TEST(test_lockout_uses_longer_of_pause_and_minimum);
    RUN_TEST(test_lockout_not_shorter_than_minimum);
    RUN_TEST(test_timeout_across_millis_wrap);
    RUN_TEST(test_backstop_cuts_pin_before_update_clears_state);
    RUN_TEST(test_off_disarms_backstop_and_out_of_range_is_rejected);
    return UNITY_END();
}
