#include <unity.h>

#include "common/Constants.h"
#include "gsm/GsmTimeParse.h"

using namespace gsm_time;

void setUp() {}
void tearDown() {}

static void test_days_from_civil_known_dates() {
    TEST_ASSERT_EQUAL_INT64(0, daysFromCivil(1970, 1, 1));
    TEST_ASSERT_EQUAL_INT64(-1, daysFromCivil(1969, 12, 31));
    TEST_ASSERT_EQUAL_INT64(10957, daysFromCivil(2000, 1, 1));
    TEST_ASSERT_EQUAL_INT64(11016, daysFromCivil(2000, 2, 29));
    TEST_ASSERT_EQUAL_INT64(20454, daysFromCivil(2026, 1, 1));
}

static void test_civil_to_epoch() {
    time_t t = 0;
    TEST_ASSERT_TRUE(civilToEpochUtc(2025, 1, 15, 12, 0, 0, t));
    TEST_ASSERT_EQUAL_INT64(1736942400LL, (int64_t)t);
    TEST_ASSERT_TRUE(civilToEpochUtc(2024, 2, 29, 0, 0, 0, t));
    TEST_ASSERT_EQUAL_INT64(1709164800LL, (int64_t)t);
}

static void test_civil_rejects_impossible_dates() {
    time_t t = 0;
    TEST_ASSERT_FALSE(civilToEpochUtc(2025, 2, 29, 0, 0, 0, t));
    TEST_ASSERT_FALSE(civilToEpochUtc(2025, 4, 31, 0, 0, 0, t));
    TEST_ASSERT_FALSE(civilToEpochUtc(2025, 13, 1, 0, 0, 0, t));
    TEST_ASSERT_FALSE(civilToEpochUtc(2025, 1, 0, 0, 0, 0, t));
    TEST_ASSERT_FALSE(civilToEpochUtc(2025, 1, 1, 24, 0, 0, t));
    TEST_ASSERT_FALSE(civilToEpochUtc(2025, 1, 1, 0, 60, 0, t));
    TEST_ASSERT_FALSE(civilToEpochUtc(2025, 1, 1, 0, 0, 61, t));
}

static void test_leap_second_rolls_over() {
    time_t a = 0, b = 0;
    TEST_ASSERT_TRUE(civilToEpochUtc(2025, 1, 1, 0, 0, 60, a));
    TEST_ASSERT_TRUE(civilToEpochUtc(2025, 1, 1, 0, 1, 0, b));
    TEST_ASSERT_EQUAL_INT64((int64_t)b, (int64_t)a);
}

static void test_cclk_moscow_offset() {
    CclkTime ct;
    // 12:00 local at +3h (12 quarters) = 09:00 UTC.
    TEST_ASSERT_TRUE(parseCclk("+CCLK: \"25/01/15,12:00:00+12\"", ct));
    TEST_ASSERT_EQUAL_INT64(1736931600LL, (int64_t)ct.utc);
    TEST_ASSERT_EQUAL_INT16(12, ct.tzQuarters);
    TEST_ASSERT_TRUE(ct.tzWholeHours);
    TEST_ASSERT_EQUAL_INT8(3, ct.tzHours);
}

static void test_cclk_negative_offset() {
    CclkTime ct;
    // 07:00 local at -5h = 12:00 UTC.
    TEST_ASSERT_TRUE(parseCclk("+CCLK: \"25/01/15,07:00:00-20\"", ct));
    TEST_ASSERT_EQUAL_INT64(1736942400LL, (int64_t)ct.utc);
    TEST_ASSERT_EQUAL_INT8(-5, ct.tzHours);
}

static void test_cclk_fractional_zone_is_parsed_but_not_whole_hours() {
    CclkTime ct;
    TEST_ASSERT_TRUE(parseCclk("+CCLK: \"25/01/15,12:00:00+22\"", ct));  // +5:30
    TEST_ASSERT_FALSE(ct.tzWholeHours);
    TEST_ASSERT_EQUAL_INT16(22, ct.tzQuarters);
}

static void test_cclk_rejects_garbage() {
    CclkTime ct;
    TEST_ASSERT_FALSE(parseCclk(nullptr, ct));
    TEST_ASSERT_FALSE(parseCclk("+CCLK: 25/01/15", ct));
    TEST_ASSERT_FALSE(parseCclk("+CCLK: \"25/13/15,12:00:00+12\"", ct));
    TEST_ASSERT_FALSE(parseCclk("+CCLK: \"25/02/30,12:00:00+12\"", ct));
    TEST_ASSERT_FALSE(parseCclk("+CCLK: \"25/01/15,12:00:00*12\"", ct));
    TEST_ASSERT_FALSE(parseCclk("+CCLK: \"25/01/15,12:00:00+99\"", ct));
    TEST_ASSERT_FALSE(parseCclk("+CSQ: 10,0", ct));
}

static void test_factory_clock_is_not_sane() {
    CclkTime ct;
    TEST_ASSERT_TRUE(parseCclk("+CCLK: \"04/01/01,00:00:12+00\"", ct));
    TEST_ASSERT_FALSE(epochSane(ct.utc));
    TEST_ASSERT_TRUE(epochSane((time_t)TimeSync::MIN_SANE_EPOCH_UTC));
}

static void test_cipgsmloc_ok() {
    time_t t = 0;
    TEST_ASSERT_TRUE(parseCipgsmloc("+CIPGSMLOC: 0,2025/01/15,12:00:00", t));
    TEST_ASSERT_EQUAL_INT64(1736942400LL, (int64_t)t);
}

static void test_cipgsmloc_errors() {
    time_t t = 0;
    TEST_ASSERT_FALSE(parseCipgsmloc("+CIPGSMLOC: 601", t));
    TEST_ASSERT_FALSE(parseCipgsmloc("+CIPGSMLOC: 1,2025/01/15,12:00:00", t));
    TEST_ASSERT_FALSE(parseCipgsmloc("+CIPGSMLOC: 0,2004/01/01,00:00:00", t));
    TEST_ASSERT_FALSE(parseCipgsmloc("+CIPGSMLOC: 0,2025/02/30,00:00:00", t));
    TEST_ASSERT_FALSE(parseCipgsmloc(nullptr, t));
}

static void test_tz_hours_to_quarters_clamps() {
    TEST_ASSERT_EQUAL_INT8(12, tzHoursToQuarters(3));
    TEST_ASSERT_EQUAL_INT8(-20, tzHoursToQuarters(-5));
    TEST_ASSERT_EQUAL_INT8(48, tzHoursToQuarters(14));
    TEST_ASSERT_EQUAL_INT8(-48, tzHoursToQuarters(-13));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_days_from_civil_known_dates);
    RUN_TEST(test_civil_to_epoch);
    RUN_TEST(test_civil_rejects_impossible_dates);
    RUN_TEST(test_leap_second_rolls_over);
    RUN_TEST(test_cclk_moscow_offset);
    RUN_TEST(test_cclk_negative_offset);
    RUN_TEST(test_cclk_fractional_zone_is_parsed_but_not_whole_hours);
    RUN_TEST(test_cclk_rejects_garbage);
    RUN_TEST(test_factory_clock_is_not_sane);
    RUN_TEST(test_cipgsmloc_ok);
    RUN_TEST(test_cipgsmloc_errors);
    RUN_TEST(test_tz_hours_to_quarters_clamps);
    return UNITY_END();
}
