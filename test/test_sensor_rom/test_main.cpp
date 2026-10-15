#include <unity.h>

#include <string.h>

#include "config/SensorRomResolve.h"

void setUp() {}
void tearDown() {}

static const uint8_t kRom[8] = {0x28, 0xFF, 0x4A, 0x01, 0x02, 0x03, 0x04, 0xB7};

static void test_format_rom() {
    char out[TextBytes::Sensors::ADDR_STRING];
    formatSensorRomString(kRom, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("28:FF:4A:01:02:03:04:B7", out);
}

static void test_format_rom_null_and_small_buffer() {
    char out[8] = "xxxxxxx";
    formatSensorRomString(nullptr, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
    char small[6];
    formatSensorRomString(kRom, small, sizeof(small));
    TEST_ASSERT_EQUAL_STRING("28:FF", small);
    formatSensorRomString(kRom, nullptr, 0);  // must not crash
}

static void test_resolve_matches_configured_rom() {
    BaseConfig cfg{};
    cfg.sensors[1].id = 7;
    strcpy(cfg.sensors[1].rom, "28:FF:4A:01:02:03:04:B7");
    TEST_ASSERT_EQUAL_UINT16(7, resolveSensorConfigId(kRom, cfg));
}

static void test_resolve_skips_unmapped_and_zero_ids() {
    BaseConfig cfg{};
    strcpy(cfg.sensors[0].rom, "28:FF:4A:01:02:03:04:B7");  // id 0: not mapped
    cfg.sensors[1].id = 3;                                   // id without ROM
    TEST_ASSERT_EQUAL_UINT16(0, resolveSensorConfigId(kRom, cfg));
    TEST_ASSERT_EQUAL_UINT16(0, resolveSensorConfigId(nullptr, cfg));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_format_rom);
    RUN_TEST(test_format_rom_null_and_small_buffer);
    RUN_TEST(test_resolve_matches_configured_rom);
    RUN_TEST(test_resolve_skips_unmapped_and_zero_ids);
    return UNITY_END();
}
