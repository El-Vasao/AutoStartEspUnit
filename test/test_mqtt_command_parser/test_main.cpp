#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "mqtt/MqttCommandParser.h"

void test_parse_run() {
    MqttCommand cmd;
    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"r1\",\"cmd\":\"run\",\"program\":3}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::Run, (int)cmd.kind);
    TEST_ASSERT_EQUAL_STRING("r1", cmd.id);
    TEST_ASSERT_EQUAL_UINT8(3, cmd.programId);
}

void test_parse_stop_list_status() {
    MqttCommand cmd;
    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"a\",\"cmd\":\"stop\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::Stop, (int)cmd.kind);

    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"b\",\"cmd\":\"list\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::List, (int)cmd.kind);

    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"c\",\"cmd\":\"status\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::Status, (int)cmd.kind);
}

void test_parse_set_thermostat() {
    MqttCommand cmd;
    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"s1\",\"cmd\":\"set\",\"name\":\"thermostat\",\"enabled\":true}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::Set, (int)cmd.kind);
    TEST_ASSERT_EQUAL((int)MqttSetName::Thermostat, (int)cmd.setName);
    TEST_ASSERT_TRUE(cmd.hasEnabled);
    TEST_ASSERT_TRUE(cmd.enabled);
}

void test_parse_set_input_ref() {
    MqttCommand cmd;
    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"s2\",\"cmd\":\"set\",\"name\":\"input\",\"ref\":1001,\"enabled\":false}",
        cmd));
    TEST_ASSERT_EQUAL((int)MqttSetName::Input, (int)cmd.setName);
    TEST_ASSERT_EQUAL_UINT16(1001, cmd.ref);
    TEST_ASSERT_TRUE(cmd.hasEnabled);
    TEST_ASSERT_FALSE(cmd.enabled);
}

void test_parse_set_time() {
    MqttCommand cmd;
    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"t1\",\"cmd\":\"set_time\",\"epoch\":1700000000}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::SetTime, (int)cmd.kind);
    TEST_ASSERT_TRUE(cmd.hasEpoch);
    TEST_ASSERT_EQUAL_UINT32(1700000000u, cmd.epoch);
}

void test_parse_rejects_unknown_cmd() {
    MqttCommand cmd;
    TEST_ASSERT_FALSE(parseMqttCommandJson("{\"id\":\"x\",\"cmd\":\"reboot\"}", cmd));
}

void test_parse_rejects_missing_id_or_cmd() {
    MqttCommand cmd;
    TEST_ASSERT_FALSE(parseMqttCommandJson("{\"cmd\":\"run\",\"program\":1}", cmd));
    TEST_ASSERT_FALSE(parseMqttCommandJson("{\"id\":\"x\"}", cmd));
    TEST_ASSERT_FALSE(parseMqttCommandJson("", cmd));
    TEST_ASSERT_FALSE(parseMqttCommandJson(nullptr, cmd));
}

void test_parse_rejects_bad_json() {
    MqttCommand cmd;
    TEST_ASSERT_FALSE(parseMqttCommandJson("{id:\"x\",\"cmd\":\"run\"}", cmd));
    TEST_ASSERT_FALSE(parseMqttCommandJson("{\"id\":\"x\",\"cmd\":\"run\"", cmd));
}

void test_extract_req_id() {
    char id[32];
    TEST_ASSERT_TRUE(mqttExtractReqId("{\"id\":\"abc\",\"cmd\":\"run\"}", id, sizeof(id)));
    TEST_ASSERT_EQUAL_STRING("abc", id);
    TEST_ASSERT_FALSE(mqttExtractReqId("{\"cmd\":\"run\"}", id, sizeof(id)));
}

void test_mqtt_err_to_http_code() {
    TEST_ASSERT_EQUAL_UINT16(200, mqttErrToHttpCode(MqttCmdErr::None));
    TEST_ASSERT_EQUAL_UINT16(400, mqttErrToHttpCode(MqttCmdErr::Parse));
    TEST_ASSERT_EQUAL_UINT16(400, mqttErrToHttpCode(MqttCmdErr::Unknown));
    TEST_ASSERT_EQUAL_UINT16(400, mqttErrToHttpCode(MqttCmdErr::Args));
    TEST_ASSERT_EQUAL_UINT16(404, mqttErrToHttpCode(MqttCmdErr::NotFound));
    TEST_ASSERT_EQUAL_UINT16(409, mqttErrToHttpCode(MqttCmdErr::Conflict));
    TEST_ASSERT_EQUAL_UINT16(422, mqttErrToHttpCode(MqttCmdErr::Rejected));
    TEST_ASSERT_EQUAL_UINT16(503, mqttErrToHttpCode(MqttCmdErr::Busy));
}

// Contract values documented in docs/modules/mqtt.md (read from the real Constants.h).
void test_contract_constants() {
    TEST_ASSERT_EQUAL_UINT16(202, MqttCmd::CODE_ACCEPTED);
    TEST_ASSERT_EQUAL_UINT8(8, MqttCmd::INBOUND_DEPTH);
    TEST_ASSERT_EQUAL_UINT8(4, MqttCmd::PENDING_REPLY_DEPTH);
    TEST_ASSERT_EQUAL_UINT8(16, MqttCmd::ID_MAX_LEN);
}

void test_parse_set_all_names() {
    struct Case {
        const char* name;
        MqttSetName expect;
    };
    const Case cases[] = {
        {"thermostat", MqttSetName::Thermostat},   {"battery_saver", MqttSetName::BatterySaver},
        {"input", MqttSetName::Input},             {"trigger", MqttSetName::Trigger},
        {"temp_trigger", MqttSetName::TempTrigger}, {"wifi_ap", MqttSetName::WifiAp},
        {"unknown_name", MqttSetName::None},
    };
    for (const Case& c : cases) {
        char json[96];
        snprintf(json, sizeof(json), "{\"id\":\"s\",\"cmd\":\"set\",\"name\":\"%s\",\"enabled\":1}", c.name);
        MqttCommand cmd;
        TEST_ASSERT_TRUE_MESSAGE(parseMqttCommandJson(json, cmd), c.name);
        TEST_ASSERT_EQUAL_MESSAGE((int)c.expect, (int)cmd.setName, c.name);
    }
}

void test_id_charset() {
    MqttCommand cmd;
    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"A_b-9\",\"cmd\":\"stop\"}", cmd));
    TEST_ASSERT_FALSE(parseMqttCommandJson("{\"id\":\"a b\",\"cmd\":\"stop\"}", cmd));
    TEST_ASSERT_FALSE(parseMqttCommandJson("{\"id\":\"a\\\"b\",\"cmd\":\"stop\"}", cmd));
}

void test_id_length_limit() {
    MqttCommand cmd;
    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"0123456789abcdef\",\"cmd\":\"stop\"}", cmd));
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef", cmd.id);
    TEST_ASSERT_FALSE(parseMqttCommandJson("{\"id\":\"0123456789abcdefX\",\"cmd\":\"stop\"}", cmd));
}

void test_trailing_garbage_rejected() {
    MqttCommand cmd;
    TEST_ASSERT_FALSE(parseMqttCommandJson("{\"id\":\"a\",\"cmd\":\"stop\"} x", cmd));
    TEST_ASSERT_TRUE(parseMqttCommandJson("  {\"id\":\"a\",\"cmd\":\"stop\"}  ", cmd));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_run);
    RUN_TEST(test_parse_stop_list_status);
    RUN_TEST(test_parse_set_thermostat);
    RUN_TEST(test_parse_set_input_ref);
    RUN_TEST(test_parse_set_time);
    RUN_TEST(test_parse_rejects_unknown_cmd);
    RUN_TEST(test_parse_rejects_missing_id_or_cmd);
    RUN_TEST(test_parse_rejects_bad_json);
    RUN_TEST(test_extract_req_id);
    RUN_TEST(test_mqtt_err_to_http_code);
    RUN_TEST(test_contract_constants);
    RUN_TEST(test_parse_set_all_names);
    RUN_TEST(test_id_charset);
    RUN_TEST(test_id_length_limit);
    RUN_TEST(test_trailing_garbage_rejected);
    return UNITY_END();
}
