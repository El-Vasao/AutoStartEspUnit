#include <unity.h>

#include "io/RpmEstimator.h"

// Подаёт фронты с фиксированной длительностью высокого и низкого уровня (мкс), начиная с `t0`; возвращает время после последнего фронта.
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
    // 1500 об/мин, 1 импульс/об, фронты CHANGE -> 2 фронта/об, период 40 мс, полупериод 20 мс.
    const uint32_t t = feedSquare(e, 1000, 20000, 20000, 20);
    assertRpmNear(1500.0f, e.rpm(t - 20000 + 1000));
}

void test_asymmetric_duty_is_exact_over_full_period() {
    RpmEstimator e;
    e.configure(2);
    // Тот же период 40 мс, скважность 10%: интервалы между фронтами 4 мс / 36 мс.
    const uint32_t t = feedSquare(e, 1000, 4000, 36000, 21);
    assertRpmNear(1500.0f, e.rpm(t - 4000 + 100));
}

void test_ppr1() {
    RpmEstimator e;
    e.configure(1);
    // Один фронт на оборот при 600 об/мин -> 100 мс между фронтами.
    uint32_t t = 0;
    for (int i = 0; i < 5; i++) { e.onEdge(t); t += 100000; }
    assertRpmNear(600.0f, e.rpm(t - 100000 + 10));
}

void test_ppr_above_ring() {
    RpmEstimator e;
    e.configure(60);
    // 60 фронтов/об при 1000 об/мин -> 1 мс между фронтами; оценка берёт RING-1 интервалов.
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
    // Пара выбросов через 50 мкс посреди полупериода (ниже предела 12000 об/мин в 2,5 мс).
    e.onEdge(lastReal + 8000);
    e.onEdge(lastReal + 8050);
    // Следующие настоящие фронты продолжают ровный сигнал.
    e.onEdge(lastReal + 20000);
    e.onEdge(lastReal + 40000);
    assertRpmNear(1500.0f, e.rpm(lastReal + 40010));
}

void test_relative_filter_rejects_fast_pair() {
    RpmEstimator e;
    e.configure(2);
    uint32_t t = feedSquare(e, 0, 20000, 20000, 10);
    const uint32_t lastReal = t - 20000;
    // Пара через 2,6 мс (выше абсолютного предела 2,5 мс) сразу после настоящего фронта:
    // её полный период (5,2 мс) меньше 1/4 предыдущего, поэтому оба фронта отбрасываются.
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

void test_value_log_due_uses_band() {
    TEST_ASSERT_TRUE(valueLogDue(0.f, 0.f, Rpm::LOG_BAND, false));
    TEST_ASSERT_FALSE(valueLogDue(0.f, 49.f, Rpm::LOG_BAND, true));
    TEST_ASSERT_TRUE(valueLogDue(0.f, 50.f, Rpm::LOG_BAND, true));
    TEST_ASSERT_TRUE(valueLogDue(200.f, 140.f, Rpm::LOG_BAND, true));
    TEST_ASSERT_FALSE(valueLogDue(20.0f, 20.4f, DS18B20::LOG_BAND_C, true));
    TEST_ASSERT_TRUE(valueLogDue(20.0f, 20.5f, DS18B20::LOG_BAND_C, true));
    TEST_ASSERT_FALSE(valueLogDue(12.60f, 12.69f, Adc::LOG_BAND_V, true));
    TEST_ASSERT_TRUE(valueLogDue(12.60f, 12.70f, Adc::LOG_BAND_V, true));
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
    RUN_TEST(test_value_log_due_uses_band);
    return UNITY_END();
}
