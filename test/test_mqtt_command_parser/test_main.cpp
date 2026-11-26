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

    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"q\",\"cmd\":\"seq\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::Seq, (int)cmd.kind);
    TEST_ASSERT_EQUAL_STRING("seq", mqttCmdKindStr(cmd.kind));

    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"h1\",\"cmd\":\"hwmap\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::HwMap, (int)cmd.kind);
    TEST_ASSERT_EQUAL_STRING("hwmap", mqttCmdKindStr(cmd.kind));

    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"t1\",\"cmd\":\"trigmap\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::TrigMap, (int)cmd.kind);
    TEST_ASSERT_EQUAL_STRING("trigmap", mqttCmdKindStr(cmd.kind));

    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"p1\",\"cmd\":\"programs\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::List, (int)cmd.kind);
}

void test_catalog_cmds_ignore_case() {
    MqttCommand cmd;
    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"h\",\"cmd\":\"hwMap\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::HwMap, (int)cmd.kind);
    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"h\",\"cmd\":\"HWMAP\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::HwMap, (int)cmd.kind);

    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"t\",\"cmd\":\"trigMap\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::TrigMap, (int)cmd.kind);
    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"t\",\"cmd\":\"TRIGMAP\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::TrigMap, (int)cmd.kind);

    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"p\",\"cmd\":\"Programs\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::List, (int)cmd.kind);
    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"p\",\"cmd\":\"PROGRAMS\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::List, (int)cmd.kind);

    TEST_ASSERT_FALSE(parseMqttCommandJson("{\"id\":\"x\",\"cmd\":\"hwmaps\"}", cmd));
    TEST_ASSERT_FALSE(parseMqttCommandJson("{\"id\":\"x\",\"cmd\":\"program\"}", cmd));
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
        "{\"id\":\"s2\",\"cmd\":\"set\",\"name\":\"input\",\"ref\":1,\"enabled\":false}",
        cmd));
    TEST_ASSERT_EQUAL((int)MqttSetName::Input, (int)cmd.setName);
    TEST_ASSERT_EQUAL_UINT16(1, cmd.ref);
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

void test_parse_reboot() {
    MqttCommand cmd;
    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"rb1\",\"cmd\":\"reboot\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::Reboot, (int)cmd.kind);
    TEST_ASSERT_EQUAL_STRING("rb1", cmd.id);
    TEST_ASSERT_EQUAL_STRING("reboot", mqttCmdKindStr(cmd.kind));
}

void test_parse_rejects_unknown_cmd() {
    MqttCommand cmd;
    TEST_ASSERT_FALSE(parseMqttCommandJson("{\"id\":\"x\",\"cmd\":\"nope\"}", cmd));
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

// Значения контракта из docs/modules/mqtt.md (читаются из настоящего Constants.h).
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

void test_parse_cfg_prg_modem() {
    MqttCommand cmd;
    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"c1\",\"cmd\":\"cfg\",\"op\":\"begin\",\"bytes\":100,\"crc\":\"aabbccdd\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::Cfg, (int)cmd.kind);
    TEST_ASSERT_EQUAL((int)MqttDebugOp::Begin, (int)cmd.debugOp);
    TEST_ASSERT_TRUE(cmd.hasDebugBytes);
    TEST_ASSERT_EQUAL_UINT32(100, cmd.debugBytes);
    TEST_ASSERT_EQUAL_STRING("aabbccdd", cmd.debugCrcHex);

    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"c2\",\"cmd\":\"cfg\",\"op\":\"put\",\"i\":0,\"n\":2,\"data\":\"YQ==\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttDebugOp::Put, (int)cmd.debugOp);
    TEST_ASSERT_TRUE(cmd.hasDebugChunk);
    TEST_ASSERT_EQUAL_UINT16(0, cmd.debugChunkI);
    TEST_ASSERT_EQUAL_UINT16(2, cmd.debugChunkN);

    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"c3\",\"cmd\":\"cfg\",\"op\":\"set\",\"path\":\"vehicle.start_method\",\"value\":\"rpm\"}",
        cmd));
    TEST_ASSERT_EQUAL((int)MqttDebugOp::Set, (int)cmd.debugOp);
    TEST_ASSERT_EQUAL_STRING("vehicle.start_method", cmd.debugPath);
    TEST_ASSERT_EQUAL_STRING("rpm", cmd.debugValue);

    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"p1\",\"cmd\":\"prg\",\"op\":\"get\",\"program\":2}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::Prg, (int)cmd.kind);
    TEST_ASSERT_EQUAL((int)MqttDebugOp::Get, (int)cmd.debugOp);
    TEST_ASSERT_EQUAL_UINT8(2, cmd.programId);

    TEST_ASSERT_TRUE(parseMqttCommandJson("{\"id\":\"m1\",\"cmd\":\"modem_reboot\"}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::ModemReboot, (int)cmd.kind);

    char b64[64];
    TEST_ASSERT_TRUE(mqttExtractDebugDataB64(
        "{\"id\":\"c2\",\"cmd\":\"cfg\",\"op\":\"put\",\"i\":0,\"n\":1,\"data\":\"YWI=\"}", b64,
        sizeof(b64)));
    TEST_ASSERT_EQUAL_STRING("YWI=", b64);
}

void test_enabled_string_and_numeric_forms() {
    MqttCommand cmd;
    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"e1\",\"cmd\":\"set\",\"name\":\"thermostat\",\"enabled\":\"true\"}", cmd));
    TEST_ASSERT_TRUE(cmd.hasEnabled);
    TEST_ASSERT_TRUE(cmd.enabled);

    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"e2\",\"cmd\":\"set\",\"name\":\"thermostat\",\"enabled\":\"0\"}", cmd));
    TEST_ASSERT_TRUE(cmd.hasEnabled);
    TEST_ASSERT_FALSE(cmd.enabled);

    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"e3\",\"cmd\":\"set\",\"name\":\"thermostat\",\"enabled\":1}", cmd));
    TEST_ASSERT_TRUE(cmd.hasEnabled);
    TEST_ASSERT_TRUE(cmd.enabled);

    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"e4\",\"cmd\":\"set\",\"name\":\"thermostat\",\"enabled\":0}", cmd));
    TEST_ASSERT_TRUE(cmd.hasEnabled);
    TEST_ASSERT_FALSE(cmd.enabled);

    TEST_ASSERT_FALSE(parseMqttCommandJson(
        "{\"id\":\"e5\",\"cmd\":\"set\",\"name\":\"thermostat\",\"enabled\":\"yes\"}", cmd));
    TEST_ASSERT_FALSE(parseMqttCommandJson(
        "{\"id\":\"e6\",\"cmd\":\"set\",\"name\":\"thermostat\",\"enabled\":2}", cmd));
}

void test_string_numeric_fields_and_unknown_keys() {
    MqttCommand cmd;
    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"s\",\"cmd\":\"run\",\"program\":\"3\",\"ref\":\"2\","
        "\"unknown\":\"v\",\"x\":1,\"y\":true}", cmd));
    TEST_ASSERT_EQUAL((int)MqttCommandKind::Run, (int)cmd.kind);
    TEST_ASSERT_EQUAL_UINT8(3, cmd.programId);
    TEST_ASSERT_EQUAL_UINT16(2, cmd.ref);

    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"p\",\"cmd\":\"run\",\"program\":\"999\"}", cmd));
    TEST_ASSERT_EQUAL_UINT8(0, cmd.programId);

    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"r\",\"cmd\":\"set\",\"name\":\"input\",\"ref\":\"70000\"}", cmd));
    TEST_ASSERT_EQUAL_UINT16(0, cmd.ref);

    // Строковые bytes/i/n/epoch сейчас не разбираются — остаются дефолтными.
    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"c\",\"cmd\":\"cfg\",\"op\":\"put\",\"bytes\":\"5\","
        "\"i\":\"1\",\"n\":\"2\",\"data\":\"YQ==\"}", cmd));
    TEST_ASSERT_FALSE(cmd.hasDebugBytes);
    TEST_ASSERT_FALSE(cmd.hasDebugChunk);

    TEST_ASSERT_TRUE(parseMqttCommandJson(
        "{\"id\":\"t\",\"cmd\":\"set_time\",\"epoch\":\"123\"}", cmd));
    TEST_ASSERT_FALSE(cmd.hasEpoch);
}

void test_debug_ops_all_and_unknown() {
    struct Case {
        const char* op;
        MqttDebugOp expect;
    };
    const Case cases[] = {
        {"begin", MqttDebugOp::Begin},   {"put", MqttDebugOp::Put},
        {"commit", MqttDebugOp::Commit}, {"abort", MqttDebugOp::Abort},
        {"get", MqttDebugOp::Get},       {"set", MqttDebugOp::Set},
        {"reset", MqttDebugOp::Reset},   {"delete", MqttDebugOp::Delete},
        {"unknown", MqttDebugOp::None},
    };
    for (const Case& c : cases) {
        char json[96];
        snprintf(json, sizeof(json), "{\"id\":\"d\",\"cmd\":\"cfg\",\"op\":\"%s\"}", c.op);
        MqttCommand cmd;
        TEST_ASSERT_TRUE_MESSAGE(parseMqttCommandJson(json, cmd), c.op);
        TEST_ASSERT_EQUAL_MESSAGE((int)c.expect, (int)cmd.debugOp, c.op);
    }
}

void test_extractors_edge_cases() {
    char id[32];
    TEST_ASSERT_FALSE(mqttExtractReqId(nullptr, id, sizeof(id)));
    TEST_ASSERT_FALSE(mqttExtractReqId("{\"id\":\"abc\"}", id, 1));
    TEST_ASSERT_FALSE(mqttExtractReqId("{\"id\":\"\"}", id, sizeof(id)));
    TEST_ASSERT_FALSE(mqttExtractReqId("{\"id\":\"0123456789abcdefX\"}", id, sizeof(id)));
    TEST_ASSERT_FALSE(mqttExtractReqId("{\"id\" \"abc\"}", id, sizeof(id)));

    char b64[16];
    TEST_ASSERT_FALSE(mqttExtractDebugDataB64(nullptr, b64, sizeof(b64)));
    TEST_ASSERT_FALSE(mqttExtractDebugDataB64("{\"cmd\":\"cfg\"}", b64, sizeof(b64)));
    TEST_ASSERT_FALSE(mqttExtractDebugDataB64("{\"data\":123}", b64, sizeof(b64)));
    TEST_ASSERT_FALSE(mqttExtractDebugDataB64("{\"data\":\"0123456789abcdef\"}", b64, sizeof(b64)));
    TEST_ASSERT_TRUE(mqttExtractDebugDataB64("{\"data\":\"YQ==\"}", b64, sizeof(b64)));
    TEST_ASSERT_EQUAL_STRING("YQ==", b64);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_run);
    RUN_TEST(test_parse_stop_list_status);
    RUN_TEST(test_catalog_cmds_ignore_case);
    RUN_TEST(test_parse_set_thermostat);
    RUN_TEST(test_parse_set_input_ref);
    RUN_TEST(test_parse_set_time);
    RUN_TEST(test_parse_reboot);
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
    RUN_TEST(test_parse_cfg_prg_modem);
    RUN_TEST(test_enabled_string_and_numeric_forms);
    RUN_TEST(test_string_numeric_fields_and_unknown_keys);
    RUN_TEST(test_debug_ops_all_and_unknown);
    RUN_TEST(test_extractors_edge_cases);
    return UNITY_END();
}
