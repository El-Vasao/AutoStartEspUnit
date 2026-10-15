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

void fillName(char* dst, size_t cap) {
    // Backslashes expand under JSON escape — worst-case name length.
    size_t i = 0;
    while (i + 1 < cap) {
        dst[i++] = '\\';
    }
    dst[i] = '\0';
}

} // namespace

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
    cfg.inputs[0].id = 1001;
    strcpy(cfg.inputs[0].name, "IN");
    BufPrint body;
    emitHwMapObject(cfg, body);
    TEST_ASSERT_EQUAL_UINT32(fnv1a32(body.data(), body.n()), hwMapObjectFnv(cfg));
}

void test_hwmap_worst_case_fits_mqtt_frame() {
    BaseConfig cfg;
    for (uint8_t i = 0; i < HardwareLimits::INPUTS; i++) {
        cfg.inputs[i].id = (uint16_t)(1001 + i);
        fillName(cfg.inputs[i].name, sizeof(cfg.inputs[i].name));
    }
    for (uint8_t i = 0; i < HardwareLimits::SENSORS; i++) {
        cfg.sensors[i].id = (uint16_t)(i + 1);
        fillName(cfg.sensors[i].name, sizeof(cfg.sensors[i].name));
    }
    cfg.input_triggers_count = Limits::MAX_TRIGGERS;
    for (uint8_t i = 0; i < Limits::MAX_TRIGGERS; i++) {
        cfg.input_triggers[i].id = (uint16_t)(10 + i);
        cfg.input_triggers[i].input_id = 1001;
        cfg.input_triggers[i].program_id = 255;
        cfg.input_triggers[i].trigger_level = 1;
    }
    cfg.temperature_triggers_count = Limits::MAX_TRIGGERS;
    for (uint8_t i = 0; i < Limits::MAX_TRIGGERS; i++) {
        cfg.temperature_triggers[i].id = (uint16_t)(20 + i);
        cfg.temperature_triggers[i].sensor_id = 65535;
        cfg.temperature_triggers[i].program_id = 255;
        strcpy(cfg.temperature_triggers[i].comparison, "below");
        cfg.temperature_triggers[i].threshold = -123.45f;
    }
    cfg.schedule_triggers_count = Limits::MAX_SCHEDULE_TRIGGERS;
    for (uint8_t i = 0; i < Limits::MAX_SCHEDULE_TRIGGERS; i++) {
        cfg.schedule_triggers[i].id = (uint16_t)(30 + i);
        cfg.schedule_triggers[i].program_id = 255;
        cfg.schedule_triggers[i].hour = 23;
        cfg.schedule_triggers[i].minute = 59;
        cfg.schedule_triggers[i].days_mask = 127;
    }

    BufPrint body;
    emitHwMapObject(cfg, body);
    const size_t envelope = strlen("{\"id\":\"0123456789abcdef\",\"code\":200,\"hw\":}");
    const size_t reply = envelope + body.n();
    TEST_ASSERT_TRUE(reply <= JsonBytes::Mqtt::HWMAP_JSON_MAX);
    // PUBLISH fixed header + remaining-length + topic length + topic (≤64).
    const size_t frame = reply + 1 + 2 + 2 + (TextBytes::Mqtt::TOPIC - 1);
    TEST_ASSERT_TRUE(frame <= NetTiming::MQTT_FRAME_MAX);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fnv1a32_known_vectors);
    RUN_TEST(test_hwmap_hash_matches_emitted_bytes);
    RUN_TEST(test_hwmap_worst_case_fits_mqtt_frame);
    return UNITY_END();
}
