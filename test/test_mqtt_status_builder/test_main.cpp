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

static BaseConfig cfgWithTriggers() {
    BaseConfig cfg;
    cfg.input_triggers_count = 2;
    cfg.input_triggers[0].id = 0;
    cfg.input_triggers[1].id = 5;
    cfg.temperature_triggers_count = 2;
    cfg.temperature_triggers[0].id = 6;
    cfg.temperature_triggers[1].id = 0;
    return cfg;
}

static void test_full_minimal_status_json() {
    StatusSnapshot s;
    BaseConfig cfg;
    BufPrint out;
    emitMqttStatusJson(s, cfg, out);
    const char* golden =
        "{\"full\":true,\"seq\":0,\"uptime\":0,\"mode\":\"\","
        "\"time\":{\"epoch\":0,\"synced\":false,\"stale\":false,\"tz\":0},"
        "\"io\":{\"voltage\":null,\"engine\":false,"
        "\"inputs\":{\"IN1\":false,\"IN2\":false,\"IN3\":false},"
        "\"relays\":{\"K1\":false,\"K2\":false,\"K3\":false,\"K4\":false,\"K5\":false},"
        "\"temps\":{}},"
        "\"csq\":0,"
        "\"program\":{\"running\":false,\"current\":null,\"last\":0,\"timerRemaining\":0},"
        "\"runtime\":{\"thermostat\":false,\"thermostatAllowed\":false,"
        "\"batterySaver\":false,\"batterySaverAllowed\":false,"
        "\"inputTriggersById\":{},\"tempTriggersById\":{}}}";
    TEST_ASSERT_EQUAL_STRING(golden, out.data());
}

static void test_full_time_stale_and_negative_tz() {
    StatusSnapshot s = sample();
    s.timeSynced = false;
    s.timeStale = true;
    s.tzOffsetHours = -5;
    BufPrint out;
    emitMqttStatusJson(s, sampleCfg(), out);
    TEST_ASSERT_NOT_NULL(
        strstr(out.data(), "\"time\":{\"epoch\":100,\"synced\":false,\"stale\":true,\"tz\":-5}"));
}

static void test_full_invalid_temp_sensor_and_negative_csq() {
    StatusSnapshot s = sample();
    s.tempSensors[1].id = 2;
    s.tempSensors[1].valid = false;
    s.csqRssi = -1;
    BufPrint out;
    emitMqttStatusJson(s, sampleCfg(), out);
    TEST_ASSERT_NOT_NULL(strstr(out.data(), "\"T2\":{\"ok\":false,\"t\":null}"));
    TEST_ASSERT_NOT_NULL(strstr(out.data(), "\"csq\":-1"));
}

static void test_full_errors_escaping_and_multiple() {
    StatusSnapshot s = sample();
    s.lastErrCount = 2;
    s.lastErr[0].code = 7;
    s.lastErr[0].active = true;
    s.lastErr[0].uptimeSec = 123;
    strcpy(s.lastErr[0].msg, "a\"b");
    s.lastErr[1].code = 9;
    s.lastErr[1].active = false;
    s.lastErr[1].uptimeSec = 456;
    strcpy(s.lastErr[1].msg, "line\n");
    BufPrint out;
    emitMqttStatusJson(s, sampleCfg(), out);
    TEST_ASSERT_NOT_NULL(strstr(
        out.data(),
        "\"err\":[{\"code\":7,\"msg\":\"a\\\"b\",\"active\":true},"
        "{\"code\":9,\"msg\":\"line\\n\",\"active\":false}]"));
}

static void test_full_triggers_skip_zero_ids() {
    StatusSnapshot s = sample();
    s.inputTriggerRuntime[1] = true;
    s.tempTriggerRuntime[0] = false;
    BufPrint out;
    emitMqttStatusJson(s, cfgWithTriggers(), out);
    TEST_ASSERT_NOT_NULL(strstr(out.data(), "\"inputTriggersById\":{\"5\":true}"));
    TEST_ASSERT_NOT_NULL(strstr(out.data(), "\"tempTriggersById\":{\"6\":false}"));
}

static void test_delta_no_changes_is_minimal() {
    StatusSnapshot prev = sample();
    StatusSnapshot cur = prev;
    cur.seq = 9;
    BufPrint out;
    emitMqttStatusDeltaJson(cur, prev, sampleCfg(), out);
    TEST_ASSERT_EQUAL_STRING("{\"full\":false,\"seq\":9}", out.data());
}

static void test_delta_uptime_mode_and_time() {
    StatusSnapshot prev = sample();
    StatusSnapshot cur = prev;
    cur.seq = 6;
    cur.uptimeSec = 99;
    strcpy(cur.modeName, "SETUP");
    cur.timeSynced = false;
    cur.timeStale = true;
    cur.tzOffsetHours = -5;
    cur.epochUtc = 1000;
    BufPrint out;
    emitMqttStatusDeltaJson(cur, prev, sampleCfg(), out);
    const char* golden =
        "{\"full\":false,\"seq\":6,\"uptime\":99,\"mode\":\"SETUP\","
        "\"time\":{\"epoch\":1000,\"synced\":false,\"stale\":true,\"tz\":-5}}";
    TEST_ASSERT_EQUAL_STRING(golden, out.data());
}

static void test_delta_each_io_field() {
    StatusSnapshot prev = sample();

    {
        StatusSnapshot cur = prev;
        cur.seq = 5;
        cur.voltageValid = false;
        BufPrint out;
        emitMqttStatusDeltaJson(cur, prev, sampleCfg(), out);
        TEST_ASSERT_EQUAL_STRING("{\"full\":false,\"seq\":5,\"io\":{\"voltage\":null}}", out.data());
    }
    {
        StatusSnapshot cur = prev;
        cur.seq = 5;
        cur.engineRunning = false;
        BufPrint out;
        emitMqttStatusDeltaJson(cur, prev, sampleCfg(), out);
        TEST_ASSERT_EQUAL_STRING("{\"full\":false,\"seq\":5,\"io\":{\"engine\":false}}", out.data());
    }
    {
        StatusSnapshot cur = prev;
        cur.seq = 5;
        cur.inputState[1] = true;
        BufPrint out;
        emitMqttStatusDeltaJson(cur, prev, sampleCfg(), out);
        TEST_ASSERT_EQUAL_STRING("{\"full\":false,\"seq\":5,\"io\":{\"inputs\":{\"IN2\":true}}}", out.data());
    }
    {
        StatusSnapshot cur = prev;
        cur.seq = 5;
        cur.relayState[4] = true;
        BufPrint out;
        emitMqttStatusDeltaJson(cur, prev, sampleCfg(), out);
        TEST_ASSERT_EQUAL_STRING("{\"full\":false,\"seq\":5,\"io\":{\"relays\":{\"K5\":true}}}", out.data());
    }
    {
        StatusSnapshot cur = prev;
        cur.seq = 5;
        cur.tempSensors[0].t = 22.0f;
        BufPrint out;
        emitMqttStatusDeltaJson(cur, prev, sampleCfg(), out);
        TEST_ASSERT_EQUAL_STRING(
            "{\"full\":false,\"seq\":5,\"io\":{\"temps\":{\"T1\":{\"ok\":true,\"t\":22.00}}}}", out.data());
    }
    {
        StatusSnapshot cur = prev;
        cur.seq = 5;
        cur.tempSensors[0].valid = false;
        BufPrint out;
        emitMqttStatusDeltaJson(cur, prev, sampleCfg(), out);
        TEST_ASSERT_EQUAL_STRING(
            "{\"full\":false,\"seq\":5,\"io\":{\"temps\":{\"T1\":{\"ok\":false,\"t\":null}}}}", out.data());
    }
    {
        // Сенсор перестал быть сопоставлен: изменение есть, но печатать нечего.
        StatusSnapshot cur = prev;
        cur.seq = 5;
        cur.tempSensors[0].id = 0;
        BufPrint out;
        emitMqttStatusDeltaJson(cur, prev, sampleCfg(), out);
        TEST_ASSERT_EQUAL_STRING("{\"full\":false,\"seq\":5,\"io\":{\"temps\":{}}}", out.data());
    }
}

static void test_delta_program_running_last_and_timer() {
    StatusSnapshot prev = sample();

    {
        StatusSnapshot cur = prev;
        cur.seq = 10;
        cur.programRunning = false;
        cur.currentProgramId = 0;
        cur.timerRemainingSec = 0;
        BufPrint out;
        emitMqttStatusDeltaJson(cur, prev, sampleCfg(), out);
        TEST_ASSERT_EQUAL_STRING(
            "{\"full\":false,\"seq\":10,\"program\":{\"running\":false,\"current\":null,\"timerRemaining\":0}}",
            out.data());
    }
    {
        StatusSnapshot cur = prev;
        cur.seq = 10;
        cur.currentProgramId = 5;
        cur.lastProgramId = 6;
        cur.timerRemainingSec = 8;
        BufPrint out;
        emitMqttStatusDeltaJson(cur, prev, sampleCfg(), out);
        TEST_ASSERT_EQUAL_STRING(
            "{\"full\":false,\"seq\":10,\"program\":{\"current\":5,\"last\":6,\"timerRemaining\":8}}",
            out.data());
    }
    {
        StatusSnapshot cur = prev;
        cur.seq = 10;
        cur.timerRemainingSec = 8;
        BufPrint out;
        emitMqttStatusDeltaJson(cur, prev, sampleCfg(), out);
        TEST_ASSERT_EQUAL_STRING(
            "{\"full\":false,\"seq\":10,\"program\":{\"timerRemaining\":8}}", out.data());
    }
}

static void test_delta_runtime_fields_and_triggers() {
    StatusSnapshot prev = sample();
    StatusSnapshot cur = prev;
    cur.seq = 11;
    cur.thermostatRuntime = false;
    cur.thermostatAllowed = false;
    cur.batterySaverRuntime = true;
    cur.batterySaverAllowed = true;
    cur.inputTriggerRuntime[1] = true;
    cur.tempTriggerRuntime[0] = true;
    BufPrint out;
    emitMqttStatusDeltaJson(cur, prev, cfgWithTriggers(), out);
    const char* golden =
        "{\"full\":false,\"seq\":11,\"runtime\":{"
        "\"thermostat\":false,\"thermostatAllowed\":false,"
        "\"batterySaver\":true,\"batterySaverAllowed\":true,"
        "\"inputTriggersById\":{\"5\":true},\"tempTriggersById\":{\"6\":true}}}";
    TEST_ASSERT_EQUAL_STRING(golden, out.data());
}

static void test_significant_and_edge_modes_time_and_io() {
    StatusSnapshot prev = sample();
    const BaseConfig cfg = sampleCfg();

    StatusSnapshot cur = prev;
    strcpy(cur.modeName, "SETUP");
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_TRUE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.timeSynced = false;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_FALSE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.timeStale = true;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_FALSE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.tzOffsetHours = 4;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_FALSE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.engineRunning = false;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_TRUE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.inputState[2] = true;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_TRUE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.csqRssi = 16;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_FALSE(mqttStatusHasEdgeChanges(cur, prev, cfg));
}

static void test_significant_and_edge_program_runtime_and_errors() {
    StatusSnapshot prev = sample();
    const BaseConfig cfg = sampleCfg();

    StatusSnapshot cur = prev;
    cur.programRunning = false;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_TRUE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.currentProgramId = 5;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_TRUE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.lastProgramId = 3;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_TRUE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.thermostatRuntime = false;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_TRUE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.batterySaverAllowed = true;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_TRUE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    cur = prev;
    cur.inputTriggerRuntime[0] = false;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_TRUE(mqttStatusHasEdgeChanges(cur, prev, cfg));

    BaseConfig tempCfg = cfg;
    tempCfg.temperature_triggers_count = 1;
    tempCfg.temperature_triggers[0].id = 6;
    cur = prev;
    cur.tempTriggerRuntime[0] = true;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, tempCfg));
    TEST_ASSERT_TRUE(mqttStatusHasEdgeChanges(cur, prev, tempCfg));

    cur = prev;
    cur.lastErrCount = 1;
    cur.lastErr[0].code = 7;
    TEST_ASSERT_TRUE(mqttStatusHasSignificantChanges(cur, prev, cfg));
    TEST_ASSERT_TRUE(mqttStatusHasEdgeChanges(cur, prev, cfg));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_full_status_matches_golden_json);
    RUN_TEST(test_invalid_voltage_is_null_and_errors_are_appended);
    RUN_TEST(test_change_thresholds_ignore_uptime_epoch_and_heap);
    RUN_TEST(test_delta_contains_only_changed_relay);
    RUN_TEST(test_delta_emits_allowed_flags);
    RUN_TEST(test_full_minimal_status_json);
    RUN_TEST(test_full_time_stale_and_negative_tz);
    RUN_TEST(test_full_invalid_temp_sensor_and_negative_csq);
    RUN_TEST(test_full_errors_escaping_and_multiple);
    RUN_TEST(test_full_triggers_skip_zero_ids);
    RUN_TEST(test_delta_no_changes_is_minimal);
    RUN_TEST(test_delta_uptime_mode_and_time);
    RUN_TEST(test_delta_each_io_field);
    RUN_TEST(test_delta_program_running_last_and_timer);
    RUN_TEST(test_delta_runtime_fields_and_triggers);
    RUN_TEST(test_significant_and_edge_modes_time_and_io);
    RUN_TEST(test_significant_and_edge_program_runtime_and_errors);
    return UNITY_END();
}
