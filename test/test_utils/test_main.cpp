#include <unity.h>

#include <string.h>

#include "common/ChannelSilk.h"
#include "common/Crc16.h"
#include "common/TimeMath.h"
#include "io/HwMap.h"

void setUp() { test_clock::set(0); }
void tearDown() {}

static void test_crc16_modbus_check_vector() {
    // Контрольное значение CRC-16/MODBUS для «123456789».
    TEST_ASSERT_EQUAL_HEX16(0x4B37, calculateCRC16("123456789", 9));
}

static void test_crc16_streaming_equals_block() {
    const char* s = "hello, crc";
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < strlen(s); i++) crc = crc16ModbusNext(crc, (uint8_t)s[i]);
    TEST_ASSERT_EQUAL_HEX16(calculateCRC16(s, strlen(s)), crc);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16ModbusFeedBytes(0xFFFF, nullptr, 4));
}

static void test_every_fires_on_interval() {
    uint32_t last = 0;
    test_clock::set(500);
    TEST_ASSERT_FALSE(every(1000, last));
    test_clock::set(1000);
    TEST_ASSERT_TRUE(every(1000, last));
    TEST_ASSERT_FALSE(every(1000, last));
    test_clock::set(1999);
    TEST_ASSERT_FALSE(every(1000, last));
    test_clock::set(2000);
    TEST_ASSERT_TRUE(every(1000, last));
}

static void test_every_survives_millis_wrap() {
    uint32_t last = 0xFFFFFF00UL;
    test_clock::set(0xFFFFFF80UL);
    TEST_ASSERT_FALSE(every(0x200, last));
    test_clock::set(0x00000100UL);  // оборот: 0x200 мс после `last`
    TEST_ASSERT_TRUE(every(0x200, last));
    TEST_ASSERT_EQUAL_HEX32(0x00000100UL, last);
}

static void test_seconds_conversions() {
    TEST_ASSERT_EQUAL_UINT32(5000, secToMs(5));
    TEST_ASSERT_EQUAL_UINT32(0, msToSecCeil(0));
    TEST_ASSERT_EQUAL_UINT32(1, msToSecCeil(1));
    TEST_ASSERT_EQUAL_UINT32(1, msToSecCeil(1000));
    TEST_ASSERT_EQUAL_UINT32(2, msToSecCeil(1001));
}

static void test_calendar_day_key_is_unique_per_day() {
    struct tm a {};
    a.tm_year = 126;  // 2026 год год
    a.tm_yday = 286;
    struct tm b = a;
    b.tm_yday = 287;
    struct tm c = a;
    c.tm_year = 127;
    TEST_ASSERT_NOT_EQUAL(calendarDayKey(a), calendarDayKey(b));
    TEST_ASSERT_NOT_EQUAL(calendarDayKey(a), calendarDayKey(c));
    struct tm a2 = a;
    a2.tm_hour = 23;
    TEST_ASSERT_EQUAL_UINT32(calendarDayKey(a), calendarDayKey(a2));
}

static void test_channel_silk_parse() {
    uint8_t n = 99;
    TEST_ASSERT_TRUE(ChannelSilk::parse("K3", ChannelSilk::Kind::Relay, &n));
    TEST_ASSERT_EQUAL_UINT8(3, n);
    TEST_ASSERT_TRUE(ChannelSilk::parse("IN2", ChannelSilk::Kind::Input, &n));
    TEST_ASSERT_EQUAL_UINT8(2, n);
    TEST_ASSERT_TRUE(ChannelSilk::parse("", ChannelSilk::Kind::Input, &n));
    TEST_ASSERT_EQUAL_UINT8(0, n);
    TEST_ASSERT_FALSE(ChannelSilk::parse("2001", ChannelSilk::Kind::Relay, &n));
    TEST_ASSERT_FALSE(ChannelSilk::parse("K6", ChannelSilk::Kind::Relay, &n));
    TEST_ASSERT_FALSE(ChannelSilk::parse("k1", ChannelSilk::Kind::Relay, &n));
}

static void test_hwmap_lookup() {
    TEST_ASSERT_EQUAL_INT8(-1, HwMap::relayIndexById(0));
    TEST_ASSERT_EQUAL_INT8(0, HwMap::relayIndexById(1));
    TEST_ASSERT_EQUAL_INT8(-1, HwMap::inputIndexById(0));
    TEST_ASSERT_EQUAL_INT8((int8_t)(HardwareLimits::INPUTS - 1),
                           HwMap::inputIndexById(HardwareLimits::INPUTS));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_crc16_modbus_check_vector);
    RUN_TEST(test_crc16_streaming_equals_block);
    RUN_TEST(test_every_fires_on_interval);
    RUN_TEST(test_every_survives_millis_wrap);
    RUN_TEST(test_seconds_conversions);
    RUN_TEST(test_calendar_day_key_is_unique_per_day);
    RUN_TEST(test_channel_silk_parse);
    RUN_TEST(test_hwmap_lookup);
    return UNITY_END();
}
