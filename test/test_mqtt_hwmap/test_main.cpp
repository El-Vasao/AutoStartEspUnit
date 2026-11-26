#include <string.h>
#include <unity.h>

#include "common/Constants.h"
#include "common/Fnv1a.h"
#include "config/ConfigTypes.h"
#include "mqtt/HwMapJson.h"

namespace {

class BufPrint : public Print {
public:
    size_t write(uint8_t c) override {
        if (n_ + 1 < sizeof(buf_)) buf_[n_] = (char)c;
        n_++;
        return 1;
    }
    size_t n() const { return n_; }
    const char* data() const { return buf_; }

private:
    char buf_[4096]{};
    size_t n_{0};
};

/// Имя канала по схеме — до 30 символов; буфер NAME = 64 — запас под UTF-8.
/// '\\' — худший случай JSON-экранирования при допустимой длине имени.
void fillName(char* dst, size_t cap) {
    constexpr size_t kSchemaNameChars = 30;
    size_t i = 0;
    while (i < kSchemaNameChars && i + 1 < cap) {
        dst[i++] = '\\';
    }
    dst[i] = '\0';
}

/// 15 кириллических букв (UTF-8) — потолок maxLength схемы; буфер NAME = 32.
void fillCyrillic15(char* dst, size_t cap) {
    static const char k15[] = u8"ЗажиганиеСалонА";
    strlcpy(dst, k15, cap);
}

void fillWorst(BaseConfig& cfg) {
    for (uint8_t i = 0; i < HardwareLimits::INPUTS; i++) {
        cfg.inputs[i].id = static_cast<uint8_t>(i + 1);
        fillName(cfg.inputs[i].name, sizeof(cfg.inputs[i].name));
    }
    for (uint8_t i = 0; i < HardwareLimits::SENSORS; i++) {
        cfg.sensors[i].id = static_cast<uint8_t>(i + 1);
        fillName(cfg.sensors[i].name, sizeof(cfg.sensors[i].name));
    }
    for (uint8_t i = 0; i < HardwareLimits::RELAYS; i++) {
        cfg.relays[i].id = static_cast<uint8_t>(i + 1);
        fillName(cfg.relays[i].name, sizeof(cfg.relays[i].name));
    }
    cfg.input_triggers_count = Limits::MAX_TRIGGERS;
    for (uint8_t i = 0; i < Limits::MAX_TRIGGERS; i++) {
        cfg.input_triggers[i].id = (uint16_t)(10 + i);
        cfg.input_triggers[i].input_id = 1;
        cfg.input_triggers[i].program_id = 255;
        cfg.input_triggers[i].trigger_level = 1;
    }
    cfg.temperature_triggers_count = Limits::MAX_TRIGGERS;
    for (uint8_t i = 0; i < Limits::MAX_TRIGGERS; i++) {
        cfg.temperature_triggers[i].id = (uint16_t)(20 + i);
        cfg.temperature_triggers[i].sensor_id = 3;
        cfg.temperature_triggers[i].program_id = 255;
        strcpy(cfg.temperature_triggers[i].comparison, "below");
        cfg.temperature_triggers[i].threshold = -123.45f;
    }
}

} // пространство имён

void test_fnv1a32_known_vectors() {
    TEST_ASSERT_EQUAL_UINT32(0x811c9dc5u, fnv1a32(static_cast<const uint8_t*>(nullptr), 0));
    TEST_ASSERT_EQUAL_UINT32(0xe40c292cu, fnv1a32("a", 1));
    Fnv1aPrint fp;
    fp.print("a");
    TEST_ASSERT_EQUAL_UINT32(0xe40c292cu, fp.hash());
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)fp.bytes());
}

void test_hwmap_hash_matches_emitted_bytes() {
    BaseConfig cfg;
    cfg.inputs[0].id = 1;
    strcpy(cfg.inputs[0].name, "IN");
    BufPrint hw;
    BufPrint trig;
    emitHwMapObject(cfg, hw);
    emitTrigMapObject(cfg, trig);
    Fnv1aPrint hwFp;
    (void)hwFp.write(reinterpret_cast<const uint8_t*>(hw.data()), hw.n());
    TEST_ASSERT_EQUAL_UINT32(hwFp.hash(), hwMapObjectFnv(cfg));
    Fnv1aPrint trigFp;
    (void)trigFp.write(reinterpret_cast<const uint8_t*>(trig.data()), trig.n());
    TEST_ASSERT_EQUAL_UINT32(trigFp.hash(), trigMapObjectFnv(cfg));
}

void test_catalog_worst_case_fits_cipsend() {
    BaseConfig cfg;
    fillWorst(cfg);

    BufPrint hw;
    emitHwMapObject(cfg, hw);
    BufPrint trig;
    emitTrigMapObject(cfg, trig);

    TEST_ASSERT_TRUE(hw.n() <= JsonBytes::Mqtt::HWMAP_TOPIC_JSON_MAX);
    TEST_ASSERT_TRUE(trig.n() <= JsonBytes::Mqtt::TRIGMAP_TOPIC_JSON_MAX);

    const size_t topicLen = TextBytes::Mqtt::TOPIC - 1;
    const size_t hwFrame = hw.n() + 1 + 2 + 2 + topicLen;
    const size_t trigFrame = trig.n() + 1 + 2 + 2 + topicLen;
    TEST_ASSERT_TRUE(hwFrame <= NetTiming::MQTT_CIPSEND_MAX);
    TEST_ASSERT_TRUE(trigFrame <= NetTiming::MQTT_CIPSEND_MAX);
    TEST_ASSERT_TRUE(hwFrame <= NetTiming::MQTT_FRAME_MAX);
    TEST_ASSERT_TRUE(trigFrame <= NetTiming::MQTT_FRAME_MAX);
}

void test_cyrillic15_names_fit_buffers_and_hwmap() {
    BaseConfig cfg;
    fillCyrillic15(cfg.inputs[0].name, sizeof(cfg.inputs[0].name));
    fillCyrillic15(cfg.sensors[0].name, sizeof(cfg.sensors[0].name));
    fillCyrillic15(cfg.relays[0].name, sizeof(cfg.relays[0].name));
    cfg.inputs[0].id = 1;
    cfg.sensors[0].id = 1;
    cfg.relays[0].id = 1;

    // 15 кириллических букв = 30 байт UTF-8; буфер NAME = 32 (место под NUL).
    TEST_ASSERT_EQUAL(30u, (unsigned)strlen(u8"ЗажиганиеСалонА"));
    TEST_ASSERT_TRUE(strlen(cfg.inputs[0].name) + 1 <= TextBytes::Inputs::NAME);
    TEST_ASSERT_TRUE(strlen(cfg.sensors[0].name) + 1 <= TextBytes::Sensors::NAME);
    TEST_ASSERT_TRUE(strlen(cfg.relays[0].name) + 1 <= TextBytes::Relays::NAME);
    TEST_ASSERT_EQUAL_STRING(u8"ЗажиганиеСалонА", cfg.inputs[0].name);
    TEST_ASSERT_EQUAL_STRING(u8"ЗажиганиеСалонА", cfg.sensors[0].name);
    TEST_ASSERT_EQUAL_STRING(u8"ЗажиганиеСалонА", cfg.relays[0].name);

    BufPrint hw;
    emitHwMapObject(cfg, hw);
    TEST_ASSERT_TRUE(hw.n() <= JsonBytes::Mqtt::HWMAP_TOPIC_JSON_MAX);
    TEST_ASSERT_NOT_NULL(strstr(hw.data(), u8"ЗажиганиеСалонА"));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fnv1a32_known_vectors);
    RUN_TEST(test_hwmap_hash_matches_emitted_bytes);
    RUN_TEST(test_catalog_worst_case_fits_cipsend);
    RUN_TEST(test_cyrillic15_names_fit_buffers_and_hwmap);
    return UNITY_END();
}
