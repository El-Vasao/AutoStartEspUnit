#include <unity.h>

#include "program/StarterMachine.h"

using namespace starter;

static Params timedParams() {
    Params p;
    p.method = StartMethod::Timed;
    p.crankMs = 2000;
    p.maxMs = 5000;
    p.settleMs = 3000;
    p.windowExtraMs = 2000;
    p.restMs = 4000;
    p.attempts = 3;
    p.runningVoltage = 13.2f;
    return p;
}

static Sample at(uint32_t ms, float v = 12.4f, bool full = true, bool engineRunning = false) {
    Sample s;
    s.nowMs = ms;
    s.engineRunning = engineRunning;
    s.voltageValid = true;
    s.voltageWindowFull = full;
    s.voltage = v;
    return s;
}

void test_timed_success_after_settle() {
    Machine m;
    m.begin(timedParams(), 0);
    Tick t = m.tick(at(0));
    TEST_ASSERT_EQUAL((int)RelayCmd::On, (int)t.relay);
    TEST_ASSERT_EQUAL((int)Phase::Cranking, (int)m.phase());

    t = m.tick(at(1999, 10.0f));
    TEST_ASSERT_EQUAL((int)RelayCmd::None, (int)t.relay);
    t = m.tick(at(2000, 10.5f));
    TEST_ASSERT_EQUAL((int)RelayCmd::Off, (int)t.relay);
    TEST_ASSERT_TRUE(t.resetVoltageWindow);
    TEST_ASSERT_EQUAL((int)Phase::Settling, (int)m.phase());

    // Высокое напряжение во время settle не проверяется до settleMs.
    t = m.tick(at(4999, 14.0f));
    TEST_ASSERT_EQUAL((int)Outcome::Running, (int)t.outcome);
    t = m.tick(at(5000, 14.0f));
    TEST_ASSERT_EQUAL((int)Outcome::Success, (int)t.outcome);
    TEST_ASSERT_TRUE(t.attemptEnded);
    TEST_ASSERT_EQUAL((int)StopReason::VoltageOk, (int)m.last().reason);
    TEST_ASSERT_EQUAL_UINT32(2000, m.last().crankMs);
    TEST_ASSERT_TRUE(m.last().hasMinVoltage);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, m.last().minVoltage);
}

void test_timed_waits_for_full_window() {
    Machine m;
    m.begin(timedParams(), 0);
    m.tick(at(0));
    m.tick(at(2000));
    Tick t = m.tick(at(5000, 14.0f, false));
    TEST_ASSERT_EQUAL((int)Outcome::Running, (int)t.outcome);
    t = m.tick(at(5800, 14.0f, true));
    TEST_ASSERT_EQUAL((int)Outcome::Success, (int)t.outcome);
}

void test_timed_window_timeout_without_voltage_fails_attempt() {
    Machine m;
    m.begin(timedParams(), 0);
    m.tick(at(0));
    m.tick(at(2000));
    Tick t = m.tick(at(7000, 12.0f, false));
    TEST_ASSERT_TRUE(t.attemptEnded);
    TEST_ASSERT_EQUAL((int)StopReason::NoVoltage, (int)m.last().reason);
    TEST_ASSERT_EQUAL((int)Phase::Resting, (int)m.phase());
}

void test_timed_window_timeout_high_voltage_without_full_window_fails() {
    Machine m;
    m.begin(timedParams(), 0);
    m.tick(at(0));
    m.tick(at(2000));
    Tick t = m.tick(at(7000, 14.0f, false));
    TEST_ASSERT_TRUE(t.attemptEnded);
    TEST_ASSERT_EQUAL((int)Outcome::Running, (int)t.outcome);
    TEST_ASSERT_EQUAL((int)StopReason::NoVoltage, (int)m.last().reason);
    TEST_ASSERT_EQUAL((int)Phase::Resting, (int)m.phase());
}

void test_timed_retries_then_failure() {
    Params p = timedParams();
    Machine m;
    m.begin(p, 0);
    uint32_t now = 0;
    int ons = 0;
    for (int attempt = 1; attempt <= p.attempts; attempt++) {
        Tick t = m.tick(at(now));
        TEST_ASSERT_EQUAL((int)RelayCmd::On, (int)t.relay);
        ons++;
        TEST_ASSERT_EQUAL_UINT8(attempt, m.attempt());
        now += p.crankMs;
        m.tick(at(now));
        now += p.settleMs;
        t = m.tick(at(now, 12.3f));
        TEST_ASSERT_TRUE(t.attemptEnded);
        TEST_ASSERT_EQUAL((int)StopReason::VoltageLow, (int)m.last().reason);
        if (attempt < p.attempts) {
            TEST_ASSERT_EQUAL((int)Outcome::Running, (int)t.outcome);
            TEST_ASSERT_EQUAL((int)Phase::Resting, (int)m.phase());
            // Пауза rest соблюдается.
            t = m.tick(at(now + p.restMs - 1));
            TEST_ASSERT_EQUAL((int)RelayCmd::None, (int)t.relay);
            now += p.restMs;
        } else {
            TEST_ASSERT_EQUAL((int)Outcome::Failure, (int)t.outcome);
        }
    }
    TEST_ASSERT_EQUAL(p.attempts, ons);
}

void test_timed_already_running_does_not_crank() {
    Machine m;
    m.begin(timedParams(), 0);
    // Флаг «работает» запрещает прокрутку даже в полосе ниже «заведён».
    Tick t = m.tick(at(0, 13.0f, true, true));
    TEST_ASSERT_EQUAL((int)Outcome::Success, (int)t.outcome);
    TEST_ASSERT_NOT_EQUAL((int)RelayCmd::On, (int)t.relay);
    TEST_ASSERT_EQUAL((int)StopReason::AlreadyRunning, (int)m.last().reason);
    TEST_ASSERT_EQUAL_UINT8(0, m.attempt());
}

void test_timed_already_running_by_voltage_without_flag() {
    // Запас: полное окно ≥ «заведён» при флаге false — тоже без прокрутки.
    Machine m;
    m.begin(timedParams(), 0);
    Tick t = m.tick(at(0, 13.8f));
    TEST_ASSERT_EQUAL((int)Outcome::Success, (int)t.outcome);
    TEST_ASSERT_EQUAL((int)StopReason::AlreadyRunning, (int)m.last().reason);
}

void test_timed_idle_waits_for_full_window_before_already_running() {
    Machine m;
    m.begin(timedParams(), 0);
    Tick t = m.tick(at(0, 13.8f, false));
    TEST_ASSERT_EQUAL((int)Outcome::Running, (int)t.outcome);
    TEST_ASSERT_EQUAL((int)RelayCmd::None, (int)t.relay);
    TEST_ASSERT_EQUAL((int)Phase::Idle, (int)m.phase());
    TEST_ASSERT_EQUAL_UINT8(0, m.attempt());

    t = m.tick(at(400, 13.8f, false));
    TEST_ASSERT_EQUAL((int)Phase::Idle, (int)m.phase());
    TEST_ASSERT_EQUAL((int)RelayCmd::None, (int)t.relay);

    t = m.tick(at(800, 13.8f, true));
    TEST_ASSERT_EQUAL((int)Outcome::Success, (int)t.outcome);
    TEST_ASSERT_EQUAL((int)StopReason::AlreadyRunning, (int)m.last().reason);
    TEST_ASSERT_EQUAL_UINT8(0, m.attempt());
}

void test_timed_flag_running_skips_window_wait() {
    Machine m;
    m.begin(timedParams(), 0);
    Tick t = m.tick(at(0, 13.0f, false, true));
    TEST_ASSERT_EQUAL((int)Outcome::Success, (int)t.outcome);
    TEST_ASSERT_EQUAL((int)StopReason::AlreadyRunning, (int)m.last().reason);
    TEST_ASSERT_NOT_EQUAL((int)RelayCmd::On, (int)t.relay);
}

void test_timed_idle_voltage_drop_before_window_cranks() {
    Machine m;
    m.begin(timedParams(), 0);
    Tick t = m.tick(at(0, 13.8f, false));
    TEST_ASSERT_EQUAL((int)Phase::Idle, (int)m.phase());
    t = m.tick(at(200, 12.0f, false));
    TEST_ASSERT_EQUAL((int)RelayCmd::On, (int)t.relay);
    TEST_ASSERT_EQUAL((int)Phase::Cranking, (int)m.phase());
    TEST_ASSERT_EQUAL_UINT8(1, m.attempt());
}

void test_timed_idle_invalid_voltage_does_not_crank() {
    Machine m;
    Params p = timedParams();
    m.begin(p, 0);
    Sample s = at(0);
    s.voltageValid = false;
    Tick t = m.tick(s);
    TEST_ASSERT_EQUAL((int)RelayCmd::None, (int)t.relay);
    TEST_ASSERT_EQUAL((int)Phase::Idle, (int)m.phase());

    s.nowMs = p.settleMs + p.windowExtraMs - 1;
    s.voltageValid = false;
    t = m.tick(s);
    TEST_ASSERT_EQUAL((int)Phase::Idle, (int)m.phase());
    TEST_ASSERT_EQUAL((int)RelayCmd::None, (int)t.relay);

    s.nowMs = p.settleMs + p.windowExtraMs;
    t = m.tick(s);
    TEST_ASSERT_TRUE(t.attemptEnded);
    TEST_ASSERT_EQUAL((int)StopReason::NoVoltage, (int)m.last().reason);
    TEST_ASSERT_EQUAL((int)Phase::Resting, (int)m.phase());
    TEST_ASSERT_NOT_EQUAL((int)RelayCmd::On, (int)t.relay);
}

void test_engine_caught_during_rest_counts_as_success() {
    Machine m;
    m.begin(timedParams(), 0);
    m.tick(at(0));
    m.tick(at(2000));
    m.tick(at(5000, 12.3f));
    TEST_ASSERT_EQUAL((int)Phase::Resting, (int)m.phase());
    Tick t = m.tick(at(9000, 13.9f));
    TEST_ASSERT_EQUAL((int)Outcome::Success, (int)t.outcome);
    TEST_ASSERT_EQUAL((int)StopReason::AlreadyRunning, (int)m.last().reason);
}

static Params condParams(StartMethod method) {
    Params p = timedParams();
    p.method = method;
    return p;
}

void test_rpm_condition_stops_immediately() {
    Machine m;
    m.begin(condParams(StartMethod::Rpm), 0);
    Tick t = m.tick(at(0));
    TEST_ASSERT_EQUAL((int)RelayCmd::On, (int)t.relay);
    Sample s = at(800);
    s.condition = true;
    t = m.tick(s);
    TEST_ASSERT_EQUAL((int)RelayCmd::Off, (int)t.relay);
    TEST_ASSERT_EQUAL((int)Outcome::Success, (int)t.outcome);
    TEST_ASSERT_FALSE(t.resetVoltageWindow);
    TEST_ASSERT_EQUAL((int)StopReason::Condition, (int)m.last().reason);
    TEST_ASSERT_EQUAL_UINT32(800, m.last().crankMs);
}

void test_rpm_ignores_crank_time_and_runs_to_max() {
    Machine m;
    m.begin(condParams(StartMethod::Rpm), 0);
    m.tick(at(0));
    Tick t = m.tick(at(2500));
    TEST_ASSERT_EQUAL((int)RelayCmd::None, (int)t.relay);
    t = m.tick(at(5000));
    TEST_ASSERT_EQUAL((int)RelayCmd::Off, (int)t.relay);
    TEST_ASSERT_EQUAL((int)StopReason::MaxTime, (int)m.last().reason);
    TEST_ASSERT_EQUAL((int)Phase::Resting, (int)m.phase());
}

void test_input_already_active_does_not_crank() {
    Machine m;
    m.begin(condParams(StartMethod::Input), 0);
    Sample s = at(0, 12.0f);
    s.condition = true;
    Tick t = m.tick(s);
    TEST_ASSERT_EQUAL((int)Outcome::Success, (int)t.outcome);
    TEST_ASSERT_NOT_EQUAL((int)RelayCmd::On, (int)t.relay);
}

void test_input_method_ignores_voltage() {
    Machine m;
    m.begin(condParams(StartMethod::Input), 0);
    // Высокое напряжение не значит «работает» для метода input.
    Tick t = m.tick(at(0, 14.0f));
    TEST_ASSERT_EQUAL((int)RelayCmd::On, (int)t.relay);
}

void test_blocked_before_start() {
    Machine m;
    m.begin(timedParams(), 0);
    Sample s = at(0);
    s.blocked = true;
    Tick t = m.tick(s);
    TEST_ASSERT_EQUAL((int)Outcome::Blocked, (int)t.outcome);
    TEST_ASSERT_EQUAL((int)RelayCmd::Off, (int)t.relay);
    TEST_ASSERT_EQUAL_UINT8(0, m.attempt());
}

void test_blocked_while_cranking() {
    Machine m;
    m.begin(condParams(StartMethod::Rpm), 0);
    m.tick(at(0));
    Sample s = at(300);
    s.blocked = true;
    Tick t = m.tick(s);
    TEST_ASSERT_EQUAL((int)RelayCmd::Off, (int)t.relay);
    TEST_ASSERT_EQUAL((int)Outcome::Blocked, (int)t.outcome);
    TEST_ASSERT_TRUE(t.attemptEnded);
    TEST_ASSERT_EQUAL((int)StopReason::Blocked, (int)m.last().reason);
}

void test_blocked_during_settling() {
    Machine m;
    m.begin(timedParams(), 0);
    m.tick(at(0));
    m.tick(at(2000));
    TEST_ASSERT_EQUAL((int)Phase::Settling, (int)m.phase());
    Sample s = at(2500, 14.0f);
    s.blocked = true;
    Tick t = m.tick(s);
    TEST_ASSERT_EQUAL((int)RelayCmd::Off, (int)t.relay);
    TEST_ASSERT_EQUAL((int)Outcome::Blocked, (int)t.outcome);
    TEST_ASSERT_EQUAL((int)Phase::Idle, (int)m.phase());
    TEST_ASSERT_EQUAL_UINT8(1, m.attempt());
}

void test_blocked_during_resting() {
    Machine m;
    m.begin(timedParams(), 0);
    m.tick(at(0));
    m.tick(at(2000));
    m.tick(at(5000, 12.0f));
    TEST_ASSERT_EQUAL((int)Phase::Resting, (int)m.phase());
    TEST_ASSERT_EQUAL((int)StopReason::VoltageLow, (int)m.last().reason);
    Sample s = at(6000, 12.0f);
    s.blocked = true;
    Tick t = m.tick(s);
    TEST_ASSERT_EQUAL((int)RelayCmd::Off, (int)t.relay);
    TEST_ASSERT_EQUAL((int)Outcome::Blocked, (int)t.outcome);
    TEST_ASSERT_EQUAL((int)Phase::Idle, (int)m.phase());
    TEST_ASSERT_EQUAL_UINT8(1, m.attempt());
}

void test_zero_rest_starts_next_attempt_same_tick() {
    Params p = condParams(StartMethod::Rpm);
    p.restMs = 0;
    Machine m;
    m.begin(p, 0);
    m.tick(at(0));
    m.tick(at(5000));
    TEST_ASSERT_EQUAL((int)Phase::Resting, (int)m.phase());
    Tick t = m.tick(at(5001));
    TEST_ASSERT_EQUAL((int)RelayCmd::On, (int)t.relay);
    TEST_ASSERT_EQUAL_UINT8(2, m.attempt());
}

void test_crank_clamped_to_max_and_gap_tracked() {
    Params p = timedParams();
    p.crankMs = 9000;
    Machine m;
    m.begin(p, 0);
    TEST_ASSERT_EQUAL_UINT32(p.maxMs, m.params().crankMs);
    m.tick(at(0));
    m.tick(at(100));
    m.tick(at(900));
    m.tick(at(5000));
    TEST_ASSERT_EQUAL((int)Phase::Settling, (int)m.phase());
    m.tick(at(8000, 13.5f));
    TEST_ASSERT_EQUAL_UINT32(4100, m.last().maxGapMs);
}

void test_timed_crank_stops_when_engine_caught() {
    Machine m;
    m.begin(timedParams(), 0);
    Tick t = m.tick(at(0));
    TEST_ASSERT_EQUAL((int)RelayCmd::On, (int)t.relay);

    t = m.tick(at(400, 11.0f));
    TEST_ASSERT_EQUAL((int)RelayCmd::None, (int)t.relay);

    // Двигатель схватился раньше crankMs: стартер снимается сразу, дальше settle и проверка окна.
    t = m.tick(at(600, 13.6f));
    TEST_ASSERT_EQUAL((int)RelayCmd::Off, (int)t.relay);
    TEST_ASSERT_TRUE(t.resetVoltageWindow);
    TEST_ASSERT_EQUAL((int)Phase::Settling, (int)m.phase());

    t = m.tick(at(3600, 13.6f));
    TEST_ASSERT_EQUAL((int)Outcome::Success, (int)t.outcome);
    TEST_ASSERT_EQUAL((int)StopReason::VoltageOk, (int)m.last().reason);
    TEST_ASSERT_EQUAL_UINT32(600, m.last().crankMs);
}

void test_timed_crank_ignores_voltage_without_full_window() {
    Machine m;
    m.begin(timedParams(), 0);
    m.tick(at(0));
    Tick t = m.tick(at(600, 13.6f, false));
    TEST_ASSERT_EQUAL((int)RelayCmd::None, (int)t.relay);
    TEST_ASSERT_EQUAL((int)Phase::Cranking, (int)m.phase());

    t = m.tick(at(2000, 13.6f, false));
    TEST_ASSERT_EQUAL((int)RelayCmd::Off, (int)t.relay);
    TEST_ASSERT_EQUAL((int)Phase::Settling, (int)m.phase());
}

void test_timed_band_below_running_still_cranks() {
    // Полоса, флаг «стоит» — крутим.
    Machine m;
    m.begin(timedParams(), 0);
    Tick t = m.tick(at(0, 13.0f));
    TEST_ASSERT_EQUAL((int)RelayCmd::On, (int)t.relay);
    TEST_ASSERT_EQUAL((int)Phase::Cranking, (int)m.phase());
}

void test_timed_band_flag_running_no_crank() {
    // Полоса, флаг «работает» — не крутим.
    Machine m;
    m.begin(timedParams(), 0);
    Tick t = m.tick(at(0, 13.0f, true, true));
    TEST_ASSERT_NOT_EQUAL((int)RelayCmd::On, (int)t.relay);
    TEST_ASSERT_EQUAL((int)Outcome::Success, (int)t.outcome);
    TEST_ASSERT_EQUAL((int)StopReason::AlreadyRunning, (int)m.last().reason);
}

void test_timed_running_threshold_is_the_boundary() {
    // Ниже «заведён» крутим; ровно на пороге уже запущен.
    Machine below;
    below.begin(timedParams(), 0);
    Tick t = below.tick(at(0, 13.19f));
    TEST_ASSERT_EQUAL((int)RelayCmd::On, (int)t.relay);

    Machine on;
    on.begin(timedParams(), 0);
    t = on.tick(at(0, 13.2f));
    TEST_ASSERT_NOT_EQUAL((int)RelayCmd::On, (int)t.relay);
    TEST_ASSERT_EQUAL((int)Outcome::Success, (int)t.outcome);
}

void test_timed_crank_continues_in_band_below_running() {
    Machine m;
    m.begin(timedParams(), 0);
    m.tick(at(0));
    Tick t = m.tick(at(600, 13.0f));
    TEST_ASSERT_EQUAL((int)RelayCmd::None, (int)t.relay);
    TEST_ASSERT_EQUAL((int)Phase::Cranking, (int)m.phase());
}

void test_timed_settle_in_band_is_failure() {
    Machine m;
    m.begin(timedParams(), 0);
    m.tick(at(0));
    m.tick(at(2000));
    Tick t = m.tick(at(4999, 12.9f));
    TEST_ASSERT_EQUAL((int)Outcome::Running, (int)t.outcome);
    t = m.tick(at(5000, 12.9f));
    TEST_ASSERT_EQUAL((int)Outcome::Running, (int)t.outcome);
    TEST_ASSERT_EQUAL((int)StopReason::VoltageLow, (int)m.last().reason);
    TEST_ASSERT_EQUAL((int)Phase::Resting, (int)m.phase());
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_timed_success_after_settle);
    RUN_TEST(test_timed_waits_for_full_window);
    RUN_TEST(test_timed_window_timeout_without_voltage_fails_attempt);
    RUN_TEST(test_timed_window_timeout_high_voltage_without_full_window_fails);
    RUN_TEST(test_timed_retries_then_failure);
    RUN_TEST(test_timed_already_running_does_not_crank);
    RUN_TEST(test_timed_already_running_by_voltage_without_flag);
    RUN_TEST(test_timed_idle_waits_for_full_window_before_already_running);
    RUN_TEST(test_timed_flag_running_skips_window_wait);
    RUN_TEST(test_timed_idle_voltage_drop_before_window_cranks);
    RUN_TEST(test_timed_idle_invalid_voltage_does_not_crank);
    RUN_TEST(test_engine_caught_during_rest_counts_as_success);
    RUN_TEST(test_rpm_condition_stops_immediately);
    RUN_TEST(test_rpm_ignores_crank_time_and_runs_to_max);
    RUN_TEST(test_input_already_active_does_not_crank);
    RUN_TEST(test_input_method_ignores_voltage);
    RUN_TEST(test_blocked_before_start);
    RUN_TEST(test_blocked_while_cranking);
    RUN_TEST(test_blocked_during_settling);
    RUN_TEST(test_blocked_during_resting);
    RUN_TEST(test_zero_rest_starts_next_attempt_same_tick);
    RUN_TEST(test_crank_clamped_to_max_and_gap_tracked);
    RUN_TEST(test_timed_crank_stops_when_engine_caught);
    RUN_TEST(test_timed_crank_ignores_voltage_without_full_window);
    RUN_TEST(test_timed_band_below_running_still_cranks);
    RUN_TEST(test_timed_band_flag_running_no_crank);
    RUN_TEST(test_timed_running_threshold_is_the_boundary);
    RUN_TEST(test_timed_crank_continues_in_band_below_running);
    RUN_TEST(test_timed_settle_in_band_is_failure);
    return UNITY_END();
}
