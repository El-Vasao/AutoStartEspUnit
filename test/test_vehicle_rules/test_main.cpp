#include <string.h>
#include <unity.h>

#include "config/VehicleRules.h"

static BaseConfig makeCfg() {
    BaseConfig c{};
    for (uint8_t i = 0; i < HardwareLimits::INPUTS; i++) {
        c.inputs[i].id = static_cast<uint16_t>(1001 + i);
        c.inputs[i].enabled = true;
        c.inputs[i].type = InputType::DIGITAL;
    }
    // Input 1002 is a pulse input with a threshold.
    c.inputs[1].type = InputType::PULSE_COUNTER;
    c.inputs[1].pulse.pulses_per_rev = 2;
    c.inputs[1].pulse.threshold_rpm = 400;
    return c;
}

static int notes = 0;
static void countNote(const char*, void*) { notes++; }

void test_start_method_strings() {
    TEST_ASSERT_EQUAL((int)StartMethod::Timed, (int)vehicle_rules::startMethodFromString("timed", StartMethod::Rpm));
    TEST_ASSERT_EQUAL((int)StartMethod::Rpm, (int)vehicle_rules::startMethodFromString("rpm", StartMethod::Timed));
    TEST_ASSERT_EQUAL((int)StartMethod::Input, (int)vehicle_rules::startMethodFromString("input", StartMethod::Timed));
    TEST_ASSERT_EQUAL((int)StartMethod::Timed, (int)vehicle_rules::startMethodFromString("bogus", StartMethod::Timed));
    TEST_ASSERT_EQUAL((int)StartMethod::Timed, (int)vehicle_rules::startMethodFromString(nullptr, StartMethod::Timed));
    TEST_ASSERT_EQUAL_STRING("rpm", vehicle_rules::startMethodToString(StartMethod::Rpm));
}

void test_defaults_are_valid() {
    BaseConfig c = makeCfg();
    TEST_ASSERT_NULL(vehicle_rules::validate(c));
    notes = 0;
    TEST_ASSERT_EQUAL_UINT8(0, vehicle_rules::normalize(c, countNote, nullptr));
    TEST_ASSERT_EQUAL(0, notes);
}

void test_normalize_out_of_range_to_defaults() {
    BaseConfig c = makeCfg();
    c.vehicle.starter_max_time_sec = 9999;
    c.vehicle.wait_after_start_sec = 0;
    c.vehicle.starter_attempts = 0;
    c.vehicle.starter_retry_pause_sec = 5000;
    c.vehicle.starter_crank_time_sec = 0.1f;
    notes = 0;
    vehicle_rules::normalize(c, countNote, nullptr);
    TEST_ASSERT_EQUAL_UINT16(StarterLimits::MAX_TIME_DEFAULT_SEC, c.vehicle.starter_max_time_sec);
    TEST_ASSERT_EQUAL_UINT16(StarterLimits::WAIT_AFTER_START_DEFAULT_SEC, c.vehicle.wait_after_start_sec);
    TEST_ASSERT_EQUAL_UINT8(StarterLimits::ATTEMPTS_DEFAULT, c.vehicle.starter_attempts);
    TEST_ASSERT_EQUAL_UINT16(StarterLimits::RETRY_PAUSE_DEFAULT_SEC, c.vehicle.starter_retry_pause_sec);
    TEST_ASSERT_EQUAL_FLOAT(StarterLimits::CRANK_TIME_DEFAULT_SEC, c.vehicle.starter_crank_time_sec);
    TEST_ASSERT_TRUE(notes >= 5);
    TEST_ASSERT_NULL(vehicle_rules::validate(c));
}

void test_normalize_clamps_crank_to_max() {
    BaseConfig c = makeCfg();
    c.vehicle.starter_max_time_sec = 3;
    c.vehicle.starter_crank_time_sec = 10.0f;
    TEST_ASSERT_NOT_NULL(vehicle_rules::validate(c));
    vehicle_rules::normalize(c, nullptr, nullptr);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, c.vehicle.starter_crank_time_sec);
    TEST_ASSERT_NULL(vehicle_rules::validate(c));
}

void test_crank_above_max_ignored_for_rpm_in_validate() {
    BaseConfig c = makeCfg();
    c.vehicle.start_method = StartMethod::Rpm;
    c.vehicle.start_input_id = 1002;
    c.vehicle.starter_max_time_sec = 3;
    c.vehicle.starter_crank_time_sec = 10.0f;
    TEST_ASSERT_NULL(vehicle_rules::validate(c));
}

void test_inverted_voltage_thresholds() {
    BaseConfig c = makeCfg();
    c.vehicle.engine_running_voltage_threshold = 12.0f;
    c.vehicle.engine_stopped_voltage_threshold = 12.5f;
    TEST_ASSERT_NOT_NULL(vehicle_rules::validate(c));
    vehicle_rules::normalize(c, nullptr, nullptr);
    TEST_ASSERT_EQUAL_FLOAT(StarterLimits::RUNNING_VOLTAGE_DEFAULT, c.vehicle.engine_running_voltage_threshold);
    TEST_ASSERT_EQUAL_FLOAT(StarterLimits::STOPPED_VOLTAGE_DEFAULT, c.vehicle.engine_stopped_voltage_threshold);
}

void test_rpm_method_input_checks() {
    BaseConfig c = makeCfg();
    c.vehicle.start_method = StartMethod::Rpm;
    c.vehicle.start_input_id = 1002;
    TEST_ASSERT_NULL(vehicle_rules::validate(c));

    c.vehicle.start_input_id = 1001;  // digital
    TEST_ASSERT_NOT_NULL(vehicle_rules::validate(c));

    c.vehicle.start_input_id = 1002;
    c.inputs[1].pulse.threshold_rpm = 0;
    TEST_ASSERT_NOT_NULL(vehicle_rules::validate(c));

    c.inputs[1].pulse.threshold_rpm = 400;
    c.inputs[1].enabled = false;
    TEST_ASSERT_NOT_NULL(vehicle_rules::validate(c));

    c.vehicle.start_input_id = 9999;
    c.inputs[1].enabled = true;
    TEST_ASSERT_NOT_NULL(vehicle_rules::validate(c));
}

void test_input_method_input_checks() {
    BaseConfig c = makeCfg();
    c.vehicle.start_method = StartMethod::Input;
    c.vehicle.start_input_id = 1001;
    TEST_ASSERT_NULL(vehicle_rules::validate(c));
    c.vehicle.start_input_id = 1002;  // pulse
    TEST_ASSERT_NOT_NULL(vehicle_rules::validate(c));
}

void test_normalize_unusable_start_input_falls_back_to_timed() {
    BaseConfig c = makeCfg();
    c.vehicle.start_method = StartMethod::Rpm;
    c.vehicle.start_input_id = 1001;
    vehicle_rules::normalize(c, nullptr, nullptr);
    TEST_ASSERT_EQUAL((int)StartMethod::Timed, (int)c.vehicle.start_method);
}

void test_block_input_checks() {
    BaseConfig c = makeCfg();
    c.vehicle.start_block_input_id = 1003;
    TEST_ASSERT_NULL(vehicle_rules::validate(c));

    c.inputs[2].enabled = false;
    TEST_ASSERT_NOT_NULL(vehicle_rules::validate(c));
    notes = 0;
    vehicle_rules::normalize(c, countNote, nullptr);
    TEST_ASSERT_EQUAL_UINT16(1003, c.vehicle.start_block_input_id);
    TEST_ASSERT_TRUE(notes >= 1);
    c.inputs[2].enabled = true;

    c.vehicle.start_block_input_id = 1002;  // pulse
    TEST_ASSERT_NOT_NULL(vehicle_rules::validate(c));

    c.vehicle.start_block_input_id = 4242;  // missing
    TEST_ASSERT_NOT_NULL(vehicle_rules::validate(c));

    c.vehicle.start_method = StartMethod::Input;
    c.vehicle.start_input_id = 1001;
    c.vehicle.start_block_input_id = 1001;  // same as start input
    TEST_ASSERT_NOT_NULL(vehicle_rules::validate(c));
}

void test_engine_indicator_pulse_needs_threshold() {
    BaseConfig c = makeCfg();
    strcpy(c.vehicle.engine_detection_source, "input");
    c.vehicle.engine_input_id = 1002;
    TEST_ASSERT_NULL(vehicle_rules::validate(c));
    c.inputs[1].pulse.threshold_rpm = 0;
    TEST_ASSERT_NOT_NULL(vehicle_rules::validate(c));
}

void test_battery_saver_thresholds() {
    BaseConfig c = makeCfg();
    c.battery_saver.voltage_start_threshold = 10.0f;
    c.battery_saver.voltage_abort_threshold = 10.5f;
    TEST_ASSERT_NOT_NULL(vehicle_rules::validate(c));
    vehicle_rules::normalize(c, nullptr, nullptr);
    TEST_ASSERT_TRUE(c.battery_saver.voltage_start_threshold > c.battery_saver.voltage_abort_threshold);
}

void test_pulses_per_rev_zero() {
    BaseConfig c = makeCfg();
    c.inputs[1].pulse.pulses_per_rev = 0;
    TEST_ASSERT_NOT_NULL(vehicle_rules::validate(c));
    vehicle_rules::normalize(c, nullptr, nullptr);
    TEST_ASSERT_EQUAL_UINT16(1, c.inputs[1].pulse.pulses_per_rev);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_start_method_strings);
    RUN_TEST(test_defaults_are_valid);
    RUN_TEST(test_normalize_out_of_range_to_defaults);
    RUN_TEST(test_normalize_clamps_crank_to_max);
    RUN_TEST(test_crank_above_max_ignored_for_rpm_in_validate);
    RUN_TEST(test_inverted_voltage_thresholds);
    RUN_TEST(test_rpm_method_input_checks);
    RUN_TEST(test_input_method_input_checks);
    RUN_TEST(test_normalize_unusable_start_input_falls_back_to_timed);
    RUN_TEST(test_block_input_checks);
    RUN_TEST(test_engine_indicator_pulse_needs_threshold);
    RUN_TEST(test_battery_saver_thresholds);
    RUN_TEST(test_pulses_per_rev_zero);
    return UNITY_END();
}
