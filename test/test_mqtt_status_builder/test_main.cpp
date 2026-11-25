#include <unity.h>

#include <string.h>

#include "common/Constants.h"
#include "config/ConfigTypes.h"
#include "mqtt/MqttStatusBuilder.h"

namespace {

class BufPrint : public Print {
public:
    size_t write(uint8_t c) override {
        if (n_ + 1 < sizeof(buf_)) {
            buf_[n_++] = static_cast<char>(c);
            buf_[n_] = '\0';
        } else {
            n_++;
        }
        return 1;
    }
    const char* data() const { return buf_; }

private:
    char buf_[2048]{};
    size_t n_{0};
};

StatusSnapshot sample() {
    StatusSnapshot s;
    s.seq = 3;
    s.uptimeSec = 12;
    strcpy(s.modeName, "NORMAL");
    s.voltageValid = true;
    s.voltage = 12.5f;
    s.engineRunning = true;
    s.inputState[0] = true;
    s.relayState[2] = true;
    s.tempSensors[0].id = 1;
    s.tempSensors[0].valid = true;
    s.tempSensors[0].t = 21.5f;
    s.programRunning = true;
    s.currentProgramId = 4;
    s.lastProgramId = 2;
    s.timerRemainingSec = 9;
    s.thermostatRuntime = true;
    s.thermostatAllowed = true;
    s.batterySaverAllowed = false;
    s.inputTriggerRuntime[0] = true;
    s.timeSynced = true;
    s.epochUtc = 100;
    s.tzOffsetHours = 3;
    s.csqRssi = 15;
    s.csqBer = 0;
    return s;
}

BaseConfig sampleCfg() {
    BaseConfig cfg;
    cfg.input_triggers_count = 1;
    cfg.input_triggers[0].id = 5;
    cfg.temperature_triggers_count = 0;
    return cfg;
}

} // namespace

void setUp() {}
void tearDown() {}

static void test_full_status_matches_golden_json() {
    BufPrint out;
    emitMqttStatusJson(sample(), sampleCfg(), out);
    const char* golden =
        "{\"full\":true,\"seq\":3,\"uptime\":12,\"mode\":\"NORMAL\","
        "\"time\":{\"epoch\":100,\"synced\":true,\"stale\":false,\"tz\":3},"
        "\"io\":{\"voltage\":12.50,\"engine\":true,"
        "\"inputs\":{\"IN1\":true,\"IN2\":false,\"IN3\":false},"
        "\"relays\":{\"K1\":false,\"K2\":false,\"K3\":true,\"K4\":false,\"K5\":false},"
        "\"temps\":{\"T1\":{\"ok\":true,\"t\":21.50}}},"
        "\"csq\":15,"
        "\"program\":{\"running\":true,\"current\":4,\"last\":2,\"timerRemaining\":9},"
        "\"runtime\":{\"thermostat\":true,\"thermostatAllowed\":true,"
        "\"batterySaver\":false,\"batterySaverAllowed\":false,"
        "\"inputTriggersById\":{\"5\":true},\"tempTriggersById\":{}}}";
    TEST_ASSERT_EQUAL_STRING(golden, out.data());
}

static void test_delta_emits_allowed_flags() {
    StatusSnapshot prev = sample();
    StatusSnapshot cur = prev;
    cur.seq = 5;
    cur.thermostatAllowed = false;
    cur.batterySaverAllowed = true;
    BufPrint out;
    emitMqttStatusDeltaJson(cur, prev, sampleCfg(), out);
    TEST_ASSERT_NOT_NULL(strstr(out.data(), "\"thermostatAllowed\":false"));
    TEST_ASSERT_NOT_NULL(strstr(out.data(), "\"batterySaverAllowed\":true"));
    TEST_ASSERT_NULL(strstr(out.data(), "\"thermostat\":"));
}

static void test_invalid_voltage_is_null_and_errors_are_appended() {
    StatusSnapshot s = sample();
    s.voltageValid = false;
    s.lastErrCount = 1;
    s.lastErr[0].code = 7;
    s.lastErr[0].active = true;
    strcpy(s.lastErr[0].msg, "relay");
    BufPrint out;
    emitMqttStatusJson(s, sampleCfg(), out);
    TEST_ASSERT_NOT_NULL(strstr(out.data(), "\"voltage\":null"));
    TEST_ASSERT_NOT_NULL(strstr(out.data(), "\"err\":[{\"code\":7,\"msg\":\"relay\",\"active\":true}]"));
}

static void test_change_thresholds_ignore_uptime_epoch_and_heap() {
    const BaseConfig cfg = sampleCfg();
    StatusSnapshot prev = sample();
    StatusSnapshot cur = prev;
    cur.uptimeSec += 5;
    cur.epochUtc += 5;
    cur.freeHeap += 100000;
    TEST_ASSERT_FALSE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_FALSE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.voltage += JsonBytes::Mqtt::STATUS_VOLTAGE_EPS - 0.01f;
    TEST_ASSERT_FALSE(mqttStatusHasSignificantChanges(cur, prev, cfg));

    cur = prev;
    cur.voltage += JsonBytes::Mqtt::STATUS_VOLTAGE_EPS;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_FALSE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.voltage += JsonBytes::Mqtt::EDGE_VOLTAGE_EPS;
    TEST_ASSERT_TRUE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.tempSensors[0].t += JsonBytes::Mqtt::EDGE_TEMP_EPS;
    TEST_ASSERT_TRUE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.timerRemainingSec += JsonBytes::Mqtt::STATUS_TIMER_EPS_SEC;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_FALSE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.relayState[0] = !cur.relayState[0];
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_TRUE(mqttStatusHasEdgeChanges(cur, prev, cfg));
}

static void test_delta_contains_only_changed_relay() {
    StatusSnapshot prev = sample();
    StatusSnapshot cur = prev;
    cur.seq = 4;
    cur.uptimeSec = 99;
    cur.relayState[0] = true;
    BufPrint out;
    emitMqttStatusDeltaJson(cur, prev, sampleCfg(), out);
    TEST_ASSERT_NOT_NULL(strstr(out.data(), "\"full\":false"));
    TEST_ASSERT_NOT_NULL(strstr(out.data(), "\"seq\":4"));
    TEST_ASSERT_NOT_NULL(strstr(out.data(), "\"uptime\":99"));
    TEST_ASSERT_NOT_NULL(strstr(out.data(), "\"relays\":{\"K1\":true}"));
    TEST_ASSERT_NULL(strstr(out.data(), "\"mode\""));
    TEST_ASSERT_NULL(strstr(out.data(), "\"voltage\""));
    TEST_ASSERT_NULL(strstr(out.data(), "\"inputs\""));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_full_status_matches_golden_json);
    RUN_TEST(test_invalid_voltage_is_null_and_errors_are_appended);
    RUN_TEST(test_change_thresholds_ignore_uptime_epoch_and_heap);
    RUN_TEST(test_delta_contains_only_changed_relay);
    RUN_TEST(test_delta_emits_allowed_flags);
    return UNITY_END();
}
