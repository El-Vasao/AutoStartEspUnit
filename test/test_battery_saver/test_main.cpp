#include <unity.h>

#include "HostFakes.h"
#include "common/Constants.h"
#include "common/TimeMath.h"
#include "config/Config.h"
#include "core/BatterySaverManager.h"
#include "core/Core.h"
#include "core/TimeSyncManager.h"
#include "gsm/GsmController.h"
#include "io/SensorsController.h"
#include "program/ProgramExecutor.h"

static GsmController& gsm() {
    static GsmController instance;
    return instance;
}

struct Harness {
    SensorsController sensors;
    ProgramExecutor exec;
    TimeSyncManager time;
    BatterySaverManager batt;
    Harness() : time(config, gsm()), batt(config, sensors, exec, time) {}
};

static void holdLow(Harness& h, uint32_t ms) {
    test_clock::advance(ms);
    h.batt.update();
}

void setUp() {
    test_clock::set(1000000);
    BatterySaverConfig& bs = config.testBase().battery_saver;
    bs = BatterySaverConfig{};
    bs.enabled = true;
    bs.voltage_start_threshold = 11.8f;
    bs.voltage_abort_threshold = 10.5f;
    bs.hysteresis = 0.5f;
    bs.min_low_voltage_duration_sec = 10;
    bs.min_time_between_attempts_sec = 30;
    bs.max_attempts_per_day = 2;
    bs.program_id = 7;
    ExecutorTestProbe::reset();
    ExecutorTestProbe::intercept = true;
    ProgramExecutorTestFlags::set(false, false);
    CoreTestProbe::engineRunning = false;
    TimeTestProbe::reset();
    SensorsTestProbe::windowResets = 0;
}

void tearDown() {}

static void test_disabled_and_invalid_voltage_do_not_start() {
    config.testBase().battery_saver.enabled = false;
    Harness off;
    off.sensors.testSetVoltage(10.0f, true);
    off.batt.begin();
    holdLow(off, secToMs(30));
    TEST_ASSERT_EQUAL_INT(0, ExecutorTestProbe::starts);

    config.testBase().battery_saver.enabled = true;
    Harness invalid;
    invalid.sensors.testSetVoltage(10.0f, false);
    invalid.batt.begin();
    holdLow(invalid, secToMs(30));
    TEST_ASSERT_EQUAL_INT(0, ExecutorTestProbe::starts);

    Harness runtimeOff;
    runtimeOff.sensors.testSetVoltage(10.0f, true);
    runtimeOff.batt.begin();
    runtimeOff.batt.setEnabled(false);
    holdLow(runtimeOff, secToMs(30));
    TEST_ASSERT_EQUAL_INT(0, ExecutorTestProbe::starts);
}

static void test_start_after_duration_then_gap_and_daily_cap() {
    Harness h;
    h.sensors.testSetVoltage(11.0f, true);
    h.batt.begin();
    h.batt.update();
    holdLow(h, secToMs(9));
    TEST_ASSERT_EQUAL_INT(0, ExecutorTestProbe::starts);

    holdLow(h, secToMs(1));
    TEST_ASSERT_EQUAL_INT(1, ExecutorTestProbe::starts);
    TEST_ASSERT_EQUAL_UINT8(7, ExecutorTestProbe::lastStartId);
    TEST_ASSERT_EQUAL_UINT8(1, h.batt.attemptsToday());

    ProgramExecutorTestFlags::set(h.exec, 0, false, false);
    h.batt.update();
    holdLow(h, secToMs(10));
    TEST_ASSERT_EQUAL_INT(1, ExecutorTestProbe::starts);

    holdLow(h, secToMs(20));
    TEST_ASSERT_EQUAL_INT(2, ExecutorTestProbe::starts);
    TEST_ASSERT_EQUAL_UINT8(2, h.batt.attemptsToday());

    ProgramExecutorTestFlags::set(h.exec, 0, false, false);
    h.batt.update();
    holdLow(h, secToMs(40));
    TEST_ASSERT_EQUAL_INT(2, ExecutorTestProbe::starts);
    TEST_ASSERT_EQUAL_UINT8(2, h.batt.attemptsToday());
}

static void test_failed_start_does_not_consume_attempt() {
    ExecutorTestProbe::startOk = false;
    Harness h;
    h.sensors.testSetVoltage(11.0f, true);
    h.batt.begin();
    h.batt.update();
    holdLow(h, secToMs(10));
    TEST_ASSERT_EQUAL_INT(1, ExecutorTestProbe::starts);
    TEST_ASSERT_EQUAL_UINT8(0, h.batt.attemptsToday());
    TEST_ASSERT_FALSE(h.exec.isRunning());
}

static void test_zero_program_does_not_call_start() {
    config.testBase().battery_saver.program_id = 0;
    Harness h;
    h.sensors.testSetVoltage(11.0f, true);
    h.batt.begin();
    h.batt.update();
    holdLow(h, secToMs(10));
    TEST_ASSERT_EQUAL_INT(0, ExecutorTestProbe::starts);
}

static void test_hysteresis_keeps_or_clears_low_timer() {
    Harness kept;
    kept.sensors.testSetVoltage(11.0f, true);
    kept.batt.begin();
    kept.batt.update();
    holdLow(kept, secToMs(5));
    kept.sensors.testSetVoltage(12.0f, true);
    kept.batt.update();
    kept.sensors.testSetVoltage(11.0f, true);
    holdLow(kept, secToMs(5));
    TEST_ASSERT_EQUAL_INT(1, ExecutorTestProbe::starts);

    ExecutorTestProbe::reset();
    Harness cleared;
    cleared.sensors.testSetVoltage(11.0f, true);
    cleared.batt.begin();
    cleared.batt.update();
    holdLow(cleared, secToMs(9));
    cleared.sensors.testSetVoltage(12.4f, true);
    cleared.batt.update();
    cleared.sensors.testSetVoltage(11.0f, true);
    cleared.batt.update();
    holdLow(cleared, secToMs(9));
    TEST_ASSERT_EQUAL_INT(0, ExecutorTestProbe::starts);
    holdLow(cleared, secToMs(1));
    TEST_ASSERT_EQUAL_INT(1, ExecutorTestProbe::starts);
}

static void test_running_program_blocks_low_voltage_start() {
    Harness h;
    h.sensors.testSetVoltage(11.0f, true);
    h.batt.begin();
    ProgramExecutorTestFlags::set(h.exec, 3, true, false);
    holdLow(h, secToMs(30));
    TEST_ASSERT_EQUAL_INT(0, ExecutorTestProbe::starts);
    TEST_ASSERT_EQUAL_INT(0, ExecutorTestProbe::stops);
}

static void test_calendar_day_resets_attempts() {
    Harness h;
    h.time.testSetSynced(true);
    TimeTestProbe::local.tm_year = 126;
    TimeTestProbe::local.tm_yday = 10;
    h.sensors.testSetVoltage(11.0f, true);
    h.batt.begin();
    h.batt.update();
    holdLow(h, secToMs(10));
    ProgramExecutorTestFlags::set(h.exec, 0, false, false);
    h.batt.update();
    holdLow(h, secToMs(30));
    TEST_ASSERT_EQUAL_UINT8(2, h.batt.attemptsToday());
    TEST_ASSERT_EQUAL_INT(2, ExecutorTestProbe::starts);

    ProgramExecutorTestFlags::set(h.exec, 0, false, false);
    TimeTestProbe::local.tm_yday = 11;
    h.batt.update();
    TEST_ASSERT_EQUAL_UINT8(0, h.batt.attemptsToday());
    holdLow(h, secToMs(30));
    TEST_ASSERT_EQUAL_UINT8(1, h.batt.attemptsToday());
    TEST_ASSERT_EQUAL_INT(3, ExecutorTestProbe::starts);
}

static void test_pseudo_day_without_clock_resets_attempts() {
    Harness h;
    h.sensors.testSetVoltage(11.0f, true);
    h.batt.begin();
    h.batt.update();
    holdLow(h, secToMs(10));
    ProgramExecutorTestFlags::set(h.exec, 0, false, false);
    h.batt.update();
    holdLow(h, secToMs(30));
    TEST_ASSERT_EQUAL_UINT8(2, h.batt.attemptsToday());

    ProgramExecutorTestFlags::set(h.exec, 0, false, false);
    test_clock::advance(Timing::MILLIS_PER_DAY);
    h.batt.update();
    TEST_ASSERT_EQUAL_UINT8(0, h.batt.attemptsToday());
    holdLow(h, secToMs(10));
    TEST_ASSERT_EQUAL_UINT8(1, h.batt.attemptsToday());
    TEST_ASSERT_EQUAL_INT(3, ExecutorTestProbe::starts);
}

static void test_abort_stops_program_unless_cranking_or_engine_running() {
    Harness aborting;
    aborting.batt.begin();
    ProgramExecutorTestFlags::set(aborting.exec, 7, true, false);
    aborting.sensors.testSetVoltage(10.0f, true);
    aborting.sensors.testSetWindowFull(true);
    aborting.batt.update();
    TEST_ASSERT_EQUAL_INT(1, ExecutorTestProbe::stops);
    TEST_ASSERT_EQUAL_INT(0, SensorsTestProbe::windowResets);

    ExecutorTestProbe::reset();
    SensorsTestProbe::windowResets = 0;
    Harness crank;
    crank.batt.begin();
    ProgramExecutorTestFlags::set(crank.exec, 7, true, true);
    crank.sensors.testSetVoltage(10.0f, true);
    crank.sensors.testSetWindowFull(true);
    crank.batt.update();
    TEST_ASSERT_EQUAL_INT(0, ExecutorTestProbe::stops);
    TEST_ASSERT_EQUAL_INT(0, SensorsTestProbe::windowResets);

    ProgramExecutorTestFlags::set(crank.exec, 7, true, false);
    crank.batt.update();
    TEST_ASSERT_EQUAL_INT(1, SensorsTestProbe::windowResets);
    TEST_ASSERT_EQUAL_INT(0, ExecutorTestProbe::stops);

    crank.sensors.testSetWindowFull(true);
    crank.batt.update();
    TEST_ASSERT_EQUAL_INT(1, ExecutorTestProbe::stops);

    ExecutorTestProbe::reset();
    Harness running;
    running.batt.begin();
    ProgramExecutorTestFlags::set(running.exec, 7, true, false);
    CoreTestProbe::engineRunning = true;
    running.sensors.testSetVoltage(10.0f, true);
    running.sensors.testSetWindowFull(true);
    running.batt.update();
    TEST_ASSERT_EQUAL_INT(0, ExecutorTestProbe::stops);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_disabled_and_invalid_voltage_do_not_start);
    RUN_TEST(test_start_after_duration_then_gap_and_daily_cap);
    RUN_TEST(test_failed_start_does_not_consume_attempt);
    RUN_TEST(test_zero_program_does_not_call_start);
    RUN_TEST(test_hysteresis_keeps_or_clears_low_timer);
    RUN_TEST(test_running_program_blocks_low_voltage_start);
    RUN_TEST(test_calendar_day_resets_attempts);
    RUN_TEST(test_pseudo_day_without_clock_resets_attempts);
    RUN_TEST(test_abort_stops_program_unless_cranking_or_engine_running);
    return UNITY_END();
}
