#include <unity.h>

#include "io/RpmEstimator.h"

// Feeds edges at fixed high/low durations (µs) starting at `t0`; returns time after the last edge.
static uint32_t feedSquare(RpmEstimator& e, uint32_t t0, uint32_t highUs, uint32_t lowUs, int edges) {
    uint32_t t = t0;
    for (int i = 0; i < edges; i++) {
        e.onEdge(t);
        t += (i % 2 == 0) ? highUs : lowUs;
    }
    return t;
}

static void assertRpmNear(float expected, float actual) {
    TEST_ASSERT_FLOAT_WITHIN(expected * 0.01f + 0.5f, expected, actual);
}

void test_steady_signal_ppr2() {
    RpmEstimator e;
    e.configure(2);
    // 1500 rpm, 1 pulse/rev, CHANGE edges -> 2 edges/rev, period 40 ms, half 20 ms.
    const uint32_t t = feedSquare(e, 1000, 20000, 20000, 20);
    assertRpmNear(1500.0f, e.rpm(t - 20000 + 1000));
}

void test_asymmetric_duty_is_exact_over_full_period() {
    RpmEstimator e;
    e.configure(2);
    // Same 40 ms period, 10% duty: per-edge intervals 4 ms / 36 ms.
    const uint32_t t = feedSquare(e, 1000, 4000, 36000, 21);
    assertRpmNear(1500.0f, e.rpm(t - 4000 + 100));
}

void test_ppr1() {
    RpmEstimator e;
    e.configure(1);
    // One edge per rev at 600 rpm -> 100 ms between edges.
    uint32_t t = 0;
    for (int i = 0; i < 5; i++) { e.onEdge(t); t += 100000; }
    assertRpmNear(600.0f, e.rpm(t - 100000 + 10));
}

void test_ppr_above_ring() {
    RpmEstimator e;
    e.configure(60);
    // 60 edges/rev at 1000 rpm -> 1 ms between edges; estimate uses RING-1 intervals.
    uint32_t t = 0;
    for (int i = 0; i < 40; i++) { e.onEdge(t); t += 1000; }
    assertRpmNear(1000.0f, e.rpm(t - 1000 + 10));
}

void test_needs_full_window_before_reporting() {
    RpmEstimator e;
    e.configure(4);
    e.onEdge(0);
    e.onEdge(10000);
    e.onEdge(20000);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e.rpm(20001));
    e.onEdge(30000);
    e.onEdge(40000);
    assertRpmNear(1500.0f, e.rpm(40001));
}

void test_zero_timeout() {
    RpmEstimator e;
    e.configure(2);
    const uint32_t t = feedSquare(e, 0, 20000, 20000, 10);
    const uint32_t last = t - 20000;
    TEST_ASSERT_TRUE(e.rpm(last + 1000) > 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e.rpm(last + Rpm::ZERO_TIMEOUT_MS * 1000UL + 1));
}

void test_gap_restarts_chain() {
    RpmEstimator e;
    e.configure(2);
    uint32_t t = feedSquare(e, 0, 20000, 20000, 10);
    t += 1000000;
    e.onEdge(t);
    TEST_ASSERT_EQUAL_UINT8(1, e.edgeCount());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e.rpm(t + 10));
}

void test_short_spike_pair_is_cancelled() {
    RpmEstimator e;
    e.configure(2);
    uint32_t t = feedSquare(e, 0, 20000, 20000, 10);
    const uint32_t lastReal = t - 20000;
    // Spike pair 50 µs apart in the middle of a half-period (below the 12000 rpm limit of 2.5 ms).
    e.onEdge(lastReal + 8000);
    e.onEdge(lastReal + 8050);
    // Next real edges continue the steady signal.
    e.onEdge(lastReal + 20000);
    e.onEdge(lastReal + 40000);
    assertRpmNear(1500.0f, e.rpm(lastReal + 40010));
}

void test_relative_filter_rejects_fast_pair() {
    RpmEstimator e;
    e.configure(2);
    uint32_t t = feedSquare(e, 0, 20000, 20000, 10);
    const uint32_t lastReal = t - 20000;
    // Pair 2.6 ms apart (above the 2.5 ms absolute limit) right after a real edge:
    // its full period (5.2 ms) is below 1/4 of the previous one, so both edges are dropped.
    e.onEdge(lastReal + 2600);
    e.onEdge(lastReal + 5200);
    e.onEdge(lastReal + 20000);
    e.onEdge(lastReal + 40000);
    assertRpmNear(1500.0f, e.rpm(lastReal + 40010));
}

void test_confirm_requires_hold() {
    RpmConfirm c;
    TEST_ASSERT_FALSE(c.update(0, 900.0f, 800));
    TEST_ASSERT_FALSE(c.update(50, 900.0f, 800));
    TEST_ASSERT_TRUE(c.update(Rpm::CONFIRM_MS, 900.0f, 800));
    TEST_ASSERT_TRUE(c.confirmed());
}

void test_confirm_resets_on_drop() {
    RpmConfirm c;
    c.update(0, 900.0f, 800);
    c.update(90, 700.0f, 800);
    TEST_ASSERT_FALSE(c.update(120, 900.0f, 800));
    TEST_ASSERT_FALSE(c.update(200, 900.0f, 800));
    TEST_ASSERT_TRUE(c.update(220, 900.0f, 800));
}

void test_confirm_zero_threshold_never() {
    RpmConfirm c;
    for (uint32_t t = 0; t < 1000; t += 10) TEST_ASSERT_FALSE(c.update(t, 5000.0f, 0));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_steady_signal_ppr2);
    RUN_TEST(test_asymmetric_duty_is_exact_over_full_period);
    RUN_TEST(test_ppr1);
    RUN_TEST(test_ppr_above_ring);
    RUN_TEST(test_needs_full_window_before_reporting);
    RUN_TEST(test_zero_timeout);
    RUN_TEST(test_gap_restarts_chain);
    RUN_TEST(test_short_spike_pair_is_cancelled);
    RUN_TEST(test_relative_filter_rejects_fast_pair);
    RUN_TEST(test_confirm_requires_hold);
    RUN_TEST(test_confirm_resets_on_drop);
    RUN_TEST(test_confirm_zero_threshold_never);
    return UNITY_END();
}
