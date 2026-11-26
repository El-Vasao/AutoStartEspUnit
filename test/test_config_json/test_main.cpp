#include <cmath>
#include <cstring>
#include <string>
#include <unity.h>

#include "common/Crc16.h"
#include "config/internal/BaseConfigJsonIo.h"
#include "config/internal/ProgramJsonIo.h"

namespace {

struct MemPrint : Print {
    std::string s;
    size_t write(uint8_t c) override {
        s.push_back(static_cast<char>(c));
        return 1;
    }
    using Print::write;
};

File fileOf(const std::string& json) { return File(json.data(), json.size()); }

bool parseCfg(const std::string& json, BaseConfig& cfg, config_internal::ParseMode mode,
              bool* hasCal = nullptr, float* calV = nullptr) {
    File f = fileOf(json);
    return config_internal::parseBaseConfigStreamingFromFile(f, cfg, mode, hasCal, calV);
}

bool parseCfgSeen(const std::string& json, BaseConfig& cfg, uint16_t* seen) {
    File f = fileOf(json);
    return config_internal::parseBaseConfigStreamingFromFile(f, cfg, config_internal::ParseMode::Strict,
                                                             nullptr, nullptr, seen);
}

bool parseProg(const std::string& json, Program& out) {
    File f = fileOf(json);
    return program_json::parseProgramObjectFile(f, out);
}

std::string stepsDoc(int n, bool close) {
    std::string s = "{\"id\":1,\"name\":\"p\",\"steps\":[";
    for (int i = 0; i < n; i++) {
        if (i) s += ',';
        s += "{\"step\":" + std::to_string(i + 1) + ",\"action\":\"WAIT\",\"ms\":1}";
    }
    if (close) s += "]}";
    return s;
}

bool stepHas(const std::string& json, const char* action, const char* needle) {
    const std::string marker = std::string("\"action\":\"") + action + "\"";
    const auto actionPos = json.find(marker);
    if (actionPos == std::string::npos) return false;
    const auto objStart = json.rfind('{', actionPos);
    const auto objEnd = json.find('}', actionPos);
    if (objStart == std::string::npos || objEnd == std::string::npos || objEnd < objStart) return false;
    const auto hit = json.find(needle, objStart);
    return hit != std::string::npos && hit < objEnd;
}

const Step* findStep(const Program& p, const char* action) {
    for (uint8_t i = 0; i < p.step_count; i++) {
        if (strcmp(p.steps[i].action, action) == 0) return &p.steps[i];
    }
    return nullptr;
}

std::string triggerArray(const char* key, int n) {
    std::string s = std::string(",\"") + key + "\":[";
    for (int i = 0; i < n; i++) {
        if (i) s += ',';
        s += "{\"id\":" + std::to_string(i + 1) + ",\"enabled\":true,\"program_id\":1}";
    }
    s += ']';
    return s;
}

int8_t tzOf(const char* literal) {
    BaseConfig cfg;
    const std::string json = std::string("{\"time\":{\"tz_offset_hours\":") + literal + "}}";
    const bool ok = parseCfg(json, cfg, config_internal::ParseMode::Strict);
    TEST_ASSERT_TRUE(ok);
    return cfg.time.tz_offset_hours;
}

} // пространство имён

void setUp() {}
void tearDown() {}

static void test_emit_parse_keeps_hidden_fields() {
    BaseConfig cfg;
    cfg.wifi.ap_timeout_sec = 90;
    cfg.vehicle.starter_crank_time_sec = 1.25f;
    cfg.vehicle.starter_relay_id = 4; // K4
    cfg.vehicle.start_block_input_id = 3; // IN3
    cfg.vehicle.start_method = StartMethod::Rpm;
    for (uint8_t i = 0; i < HardwareLimits::INPUTS; i++) cfg.inputs[i].id = static_cast<uint8_t>(i + 1);
    cfg.inputs[1].type = InputType::PULSE_COUNTER;
    cfg.inputs[1].pulse.pulses_per_rev = 4;
    cfg.inputs[1].pulse.threshold_rpm = 800;

    MemPrint out;
    const size_t n = config_internal::serializeBaseConfigToPrint(cfg, out);
    TEST_ASSERT_EQUAL_UINT32(out.s.size(), n);
    TEST_ASSERT_NULL(strstr(out.s.c_str(), "adc_calibrate_voltage"));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"starter_relay_id\":\"K4\""));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"start_block_input_id\":\"IN3\""));

    BaseConfig back;
    bool hasCal = true;
    float calV = 99.0f;
    TEST_ASSERT_TRUE(parseCfg(out.s, back, config_internal::ParseMode::Strict, &hasCal, &calV));
    TEST_ASSERT_FALSE(hasCal);
    TEST_ASSERT_EQUAL_UINT16(90, back.wifi.ap_timeout_sec);
    TEST_ASSERT_EQUAL_FLOAT(1.25f, back.vehicle.starter_crank_time_sec);
    TEST_ASSERT_EQUAL_UINT8(4, back.vehicle.starter_relay_id);
    TEST_ASSERT_EQUAL_UINT8(3, back.vehicle.start_block_input_id);
    TEST_ASSERT_EQUAL((int)StartMethod::Rpm, (int)back.vehicle.start_method);
    TEST_ASSERT_EQUAL_UINT16(4, back.inputs[1].pulse.pulses_per_rev);
    TEST_ASSERT_EQUAL_UINT16(800, back.inputs[1].pulse.threshold_rpm);
    TEST_ASSERT_EQUAL((int)InputType::PULSE_COUNTER, (int)back.inputs[1].type);
    for (uint8_t i = 0; i < HardwareLimits::INPUTS; i++) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(i + 1), back.inputs[i].id);
    }
}

static void test_adc_calibrate_voltage_is_out_param_only() {
    BaseConfig cfg;
    bool hasCal = false;
    float calV = 0.0f;
    const char* json =
        "{\"vehicle\":{\"adc_voltage_coeff\":0.025,\"adc_calibrate_voltage\":13.4}}";
    TEST_ASSERT_TRUE(parseCfg(json, cfg, config_internal::ParseMode::Strict, &hasCal, &calV));
    TEST_ASSERT_TRUE(hasCal);
    TEST_ASSERT_EQUAL_FLOAT(13.4f, calV);
    TEST_ASSERT_EQUAL_FLOAT(0.025f, cfg.vehicle.adc_voltage_coeff);

    MemPrint out;
    config_internal::serializeBaseConfigToPrint(cfg, out);
    TEST_ASSERT_NULL(strstr(out.s.c_str(), "adc_calibrate_voltage"));
}

static void test_unknown_keys_and_extra_arrays_trim() {
    std::string json =
        "{\"not_a_field\":1,\"nested_unknown\":{\"x\":[1,{\"y\":true}]},"
        "\"weird_arr\":[1,2,{\"z\":true}],"
        "\"wifi\":{\"ssid\":\"Kept\",\"mystery\":false,\"nested\":{\"a\":1}},"
        "\"sensors\":[{\"id\":\"T1\",\"name\":\"a\"},{\"id\":\"T2\",\"name\":\"b\"},"
        "{\"id\":\"T3\",\"name\":\"c\"},{\"id\":\"T1\",\"name\":\"d\"},{\"id\":\"T2\",\"name\":\"e\"}],"
        "\"inputs\":[{\"id\":\"IN1\",\"name\":\"i0\"},{\"id\":\"IN2\",\"name\":\"i1\"},"
        "{\"id\":\"IN3\",\"name\":\"i2\"},{\"id\":\"IN1\",\"name\":\"i3\"}]";
    json += triggerArray("input_triggers", Limits::MAX_TRIGGERS + 1);
    json += triggerArray("temperature_triggers", Limits::MAX_TRIGGERS + 1);
    json += '}';

    BaseConfig cfg;
    TEST_ASSERT_TRUE(parseCfg(json, cfg, config_internal::ParseMode::Strict));
    TEST_ASSERT_EQUAL_STRING("Kept", cfg.wifi.ssid);
    TEST_ASSERT_EQUAL_STRING("12345678", cfg.wifi.password);
    TEST_ASSERT_EQUAL_UINT8(1, cfg.sensors[0].id);
    TEST_ASSERT_EQUAL_UINT8(2, cfg.sensors[1].id);
    TEST_ASSERT_EQUAL_UINT8(3, cfg.sensors[2].id);
    TEST_ASSERT_EQUAL_STRING("c", cfg.sensors[2].name);
    TEST_ASSERT_EQUAL_STRING("i0", cfg.inputs[0].name);
    TEST_ASSERT_EQUAL_STRING("i2", cfg.inputs[2].name);
    TEST_ASSERT_EQUAL_UINT8(Limits::MAX_TRIGGERS, cfg.input_triggers_count);
    TEST_ASSERT_EQUAL_UINT16(1, cfg.input_triggers[0].id);
    TEST_ASSERT_EQUAL_UINT16(Limits::MAX_TRIGGERS, cfg.input_triggers[Limits::MAX_TRIGGERS - 1].id);
    TEST_ASSERT_EQUAL_UINT8(Limits::MAX_TRIGGERS, cfg.temperature_triggers_count);
    TEST_ASSERT_EQUAL_UINT16(Limits::MAX_TRIGGERS, cfg.temperature_triggers[Limits::MAX_TRIGGERS - 1].id);
}

static void test_digital_active_state_defaults_and_pulse_type() {
    BaseConfig cfg;
    const char* json =
        "{\"inputs\":["
        "{\"id\":\"IN1\",\"enabled\":true,\"type\":0,\"name\":\"door\"},"
        "{\"id\":\"IN2\",\"type\":1,\"pulse\":{\"pulses_per_rev\":3,\"threshold_rpm\":450}}"
        "]}";
    TEST_ASSERT_TRUE(parseCfg(json, cfg, config_internal::ParseMode::Strict));
    TEST_ASSERT_EQUAL_UINT8(1, cfg.inputs[0].id);
    TEST_ASSERT_EQUAL_UINT8(2, cfg.inputs[1].id);
    TEST_ASSERT_TRUE(cfg.inputs[0].active_state);
    TEST_ASSERT_EQUAL((int)InputType::DIGITAL, (int)cfg.inputs[0].type);
    TEST_ASSERT_EQUAL((int)InputType::PULSE_COUNTER, (int)cfg.inputs[1].type);
    TEST_ASSERT_EQUAL_UINT16(3, cfg.inputs[1].pulse.pulses_per_rev);
    TEST_ASSERT_EQUAL_UINT16(450, cfg.inputs[1].pulse.threshold_rpm);
}

static void test_truncated_json_strict_fails_lenient_keeps() {
    // Принятые настройки — Strict. Загрузка повреждённого /config.json — Lenient и сохраняет
    // поля, которые парсер уже принял.
    const char* json =
        "{\"setup_required\":true,\"wifi\":{\"ssid\":\"Kept\",\"ap_timeout_sec\":42},"
        "\"vehicle\":{\"starter_relay_id\":\"K5\",\"starter_crank_time_sec\":1.5}";
    BaseConfig strictCfg;
    TEST_ASSERT_FALSE(parseCfg(json, strictCfg, config_internal::ParseMode::Strict));

    BaseConfig cfg;
    strcpy(cfg.mqtt.broker, "keep-me");
    TEST_ASSERT_TRUE(parseCfg(json, cfg, config_internal::ParseMode::Lenient));
    TEST_ASSERT_TRUE(cfg.setup_required);
    TEST_ASSERT_EQUAL_STRING("Kept", cfg.wifi.ssid);
    TEST_ASSERT_EQUAL_UINT16(42, cfg.wifi.ap_timeout_sec);
    TEST_ASSERT_EQUAL_UINT8(5, cfg.vehicle.starter_relay_id);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, cfg.vehicle.starter_crank_time_sec);
    TEST_ASSERT_EQUAL_STRING("keep-me", cfg.mqtt.broker);
}

static void test_program_unclosed_steps_fails() {
    Program p;
    TEST_ASSERT_FALSE(parseProg(stepsDoc(1, false), p));
}

static void test_program_empty_action_fails() {
    Program p;
    TEST_ASSERT_FALSE(parseProg("{\"id\":1,\"name\":\"p\",\"steps\":[{\"step\":1,\"action\":\"\"}]}", p));
}

static void test_program_id_zero_fails() {
    Program p;
    TEST_ASSERT_FALSE(parseProg("{\"id\":0,\"name\":\"p\",\"steps\":[{\"step\":1,\"action\":\"WAIT\",\"ms\":1}]}", p));
}

static void test_program_file_id_must_match_expected() {
    const char* json = "{\"id\":2,\"name\":\"n\",\"steps\":[{\"action\":\"WAIT\",\"ms\":5}]}";
    CompiledStep steps[4];
    uint8_t count = 99;
    char name[TextBytes::Programs::NAME];
    File mismatch = fileOf(json);
    TEST_ASSERT_FALSE(program_json::parseProgramCompiledFile(mismatch, 3, steps, 4, &count, name, sizeof name));
    File match = fileOf(json);
    TEST_ASSERT_TRUE(program_json::parseProgramCompiledFile(match, 2, steps, 4, &count, name, sizeof name));
    TEST_ASSERT_EQUAL_UINT8(1, count);
    TEST_ASSERT_EQUAL((int)ActionId::WAIT, (int)steps[0].action);
    TEST_ASSERT_EQUAL_STRING("n", name);
}

static void test_program_step_past_max_fails() {
    Program ok;
    TEST_ASSERT_TRUE(parseProg(stepsDoc(Limits::MAX_STEPS_PER_PROGRAM, true), ok));
    TEST_ASSERT_EQUAL_UINT8(Limits::MAX_STEPS_PER_PROGRAM, ok.step_count);
    Program over;
    TEST_ASSERT_FALSE(parseProg(stepsDoc(Limits::MAX_STEPS_PER_PROGRAM + 1, true), over));
}

static void test_program_step_round_trip() {
    Program p;
    p.id = 4;
    strcpy(p.name, "Night");
    p.step_count = 7;

    p.steps[0].step = 1;
    strcpy(p.steps[0].action, "RELAY_ON");
    p.steps[0].relay_id = 3; // K3

    p.steps[1].step = 2;
    strcpy(p.steps[1].action, "WAIT");
    p.steps[1].ms = 1500;

    p.steps[2].step = 3;
    strcpy(p.steps[2].action, "INPUT_ENABLE");
    p.steps[2].input_id = 2; // IN2

    p.steps[3].step = 4;
    strcpy(p.steps[3].action, "INPUT_TRIGGER_ENABLE");
    p.steps[3].input_trigger_id = 7;

    p.steps[4].step = 5;
    strcpy(p.steps[4].action, "TEMP_TRIGGER_ENABLE");
    p.steps[4].temp_trigger_id = 9;

    p.steps[5].step = 6;
    strcpy(p.steps[5].action, "SMS_OWNER");
    strcpy(p.steps[5].message, "say \"hi\"");

    p.steps[6].step = 7;
    strcpy(p.steps[6].action, "ENGINE_START");
    p.steps[6].relay_id = 2;

    MemPrint out;
    program_json::emitProgramToPrint(p, out);
    TEST_ASSERT_TRUE(stepHas(out.s, "RELAY_ON", "\"relay_id\":\"K3\""));
    TEST_ASSERT_TRUE(stepHas(out.s, "WAIT", "\"ms\":1500"));
    TEST_ASSERT_TRUE(stepHas(out.s, "INPUT_ENABLE", "\"input_id\":\"IN2\""));
    TEST_ASSERT_TRUE(stepHas(out.s, "INPUT_TRIGGER_ENABLE", "\"input_trigger_id\":7"));
    TEST_ASSERT_TRUE(stepHas(out.s, "TEMP_TRIGGER_ENABLE", "\"temp_trigger_id\":9"));
    TEST_ASSERT_TRUE(stepHas(out.s, "SMS_OWNER", "\"message\":\"say \\\"hi\\\"\""));
    TEST_ASSERT_FALSE(stepHas(out.s, "ENGINE_START", "relay_id"));

    Program back;
    TEST_ASSERT_TRUE(parseProg(out.s, back));
    TEST_ASSERT_EQUAL_UINT8(4, back.id);
    TEST_ASSERT_EQUAL_STRING("Night", back.name);
    TEST_ASSERT_EQUAL_UINT8(7, back.step_count);
    const Step* relay = findStep(back, "RELAY_ON");
    const Step* wait = findStep(back, "WAIT");
    const Step* input = findStep(back, "INPUT_ENABLE");
    const Step* inTrig = findStep(back, "INPUT_TRIGGER_ENABLE");
    const Step* tempTrig = findStep(back, "TEMP_TRIGGER_ENABLE");
    const Step* sms = findStep(back, "SMS_OWNER");
    const Step* start = findStep(back, "ENGINE_START");
    TEST_ASSERT_NOT_NULL(relay);
    TEST_ASSERT_NOT_NULL(wait);
    TEST_ASSERT_NOT_NULL(input);
    TEST_ASSERT_NOT_NULL(inTrig);
    TEST_ASSERT_NOT_NULL(tempTrig);
    TEST_ASSERT_NOT_NULL(sms);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_EQUAL_UINT8(3, relay->relay_id);
    TEST_ASSERT_EQUAL_UINT32(1500, wait->ms);
    TEST_ASSERT_EQUAL_UINT8(2, input->input_id);
    TEST_ASSERT_EQUAL_UINT16(7, inTrig->input_trigger_id);
    TEST_ASSERT_EQUAL_UINT16(9, tempTrig->temp_trigger_id);
    TEST_ASSERT_EQUAL_STRING("say \"hi\"", sms->message);
    TEST_ASSERT_EQUAL_UINT8(0, start->relay_id);
}

static void test_battery_saver_u16_overflow_fails_strict() {
    BaseConfig cfg;
    const char* json = "{\"battery_saver\":{\"max_attempts_per_day\":300}}";
    TEST_ASSERT_FALSE(parseCfg(json, cfg, config_internal::ParseMode::Strict));
}

static void test_tz_offset_out_of_range_keeps_previous() {
    // parseI16 ограничивает значение int16. Приведение к int8_t завернуло бы 300→44, 256→0, 128→-128, 32767→-1.
    // Вне -12..14 остаётся уже записанное значение (начальный default или более ранний ключ).
    TEST_ASSERT_EQUAL_INT8(3, tzOf("15"));
    TEST_ASSERT_EQUAL_INT8(3, tzOf("-13"));
    TEST_ASSERT_EQUAL_INT8(3, tzOf("300"));
    TEST_ASSERT_EQUAL_INT8(3, tzOf("256"));
    TEST_ASSERT_EQUAL_INT8(3, tzOf("128"));
    TEST_ASSERT_EQUAL_INT8(3, tzOf("99999"));
    TEST_ASSERT_EQUAL_INT8(-12, tzOf("-12"));
    TEST_ASSERT_EQUAL_INT8(14, tzOf("14"));
    TEST_ASSERT_EQUAL_INT8(0, tzOf("0"));
    TEST_ASSERT_EQUAL_INT8(5, tzOf("5"));

    BaseConfig cfg;
    const char* dup = "{\"time\":{\"tz_offset_hours\":5,\"tz_offset_hours\":300}}";
    TEST_ASSERT_TRUE(parseCfg(dup, cfg, config_internal::ParseMode::Strict));
    TEST_ASSERT_EQUAL_INT8(5, cfg.time.tz_offset_hours);
}

static void fillFullConfig(BaseConfig& cfg) {
    cfg.setup_required = true;

    strcpy(cfg.wifi.ssid, "MyCar");
    strcpy(cfg.wifi.password, "pa\"ss");
    cfg.wifi.ap_timeout_sec = 90;
    cfg.wifi.ap_timeout_enabled = true;

    strcpy(cfg.gsm.apn, "internet");
    strcpy(cfg.gsm.apn_user, "user");
    strcpy(cfg.gsm.apn_pass, "pass");
    strcpy(cfg.gsm.owner_phone, "+71234567890");
    strcpy(cfg.gsm.dtmf_password, "1234");

    strcpy(cfg.mqtt.broker, "broker.local");
    cfg.mqtt.port = 1884;
    strcpy(cfg.mqtt.client_id, "dev1");
    strcpy(cfg.mqtt.user, "mqtt-user");
    strcpy(cfg.mqtt.pass, "mqtt-pass");
    strcpy(cfg.mqtt.topic_prefix, "car/x");
    cfg.mqtt.publish_interval_sec = 30;

    cfg.vehicle.adc_voltage_coeff = 0.012345678f;
    cfg.vehicle.starter_max_time_sec = 20;
    cfg.vehicle.wait_after_start_sec = 7;
    cfg.vehicle.engine_running_voltage_threshold = 13.4f;
    cfg.vehicle.engine_stopped_voltage_threshold = 12.2f;
    cfg.vehicle.starter_relay_id = 4;
    cfg.vehicle.start_method = StartMethod::Rpm;
    cfg.vehicle.start_input_id = 1;
    cfg.vehicle.starter_crank_time_sec = 1.75f;
    cfg.vehicle.starter_retry_pause_sec = 4;
    cfg.vehicle.starter_attempts = 3;
    cfg.vehicle.start_block_input_id = 2;
    strcpy(cfg.vehicle.engine_detection_source, "input");
    cfg.vehicle.engine_input_id = 3;

    for (uint8_t i = 0; i < HardwareLimits::SENSORS; i++) {
        cfg.sensors[i].id = static_cast<uint8_t>(i + 1);
        cfg.sensors[i].coeff = (i == 0) ? 0.004f : 1.0f;
    }
    strcpy(cfg.sensors[0].name, "Cabin");
    strcpy(cfg.sensors[0].rom, "AA:BB:CC:DD:EE:FF:11:22");
    strcpy(cfg.sensors[1].name, "Engine");

    for (uint8_t i = 0; i < HardwareLimits::INPUTS; i++) {
        cfg.inputs[i].id = static_cast<uint8_t>(i + 1);
        cfg.inputs[i].enabled = (i != 1);
    }
    strcpy(cfg.inputs[0].name, "Brake");
    cfg.inputs[0].active_state = false;
    cfg.inputs[1].type = InputType::PULSE_COUNTER;
    strcpy(cfg.inputs[1].name, "RPM");
    cfg.inputs[1].pulse.pulses_per_rev = 4;
    cfg.inputs[1].pulse.threshold_rpm = 800;

    for (uint8_t i = 0; i < HardwareLimits::RELAYS; i++) {
        cfg.relays[i].id = static_cast<uint8_t>(i + 1);
    }
    strcpy(cfg.relays[0].name, "ACC");
    strcpy(cfg.relays[1].name, "IGN");

    cfg.thermostat.enabled = true;
    cfg.thermostat.sensor_id = 1;
    strcpy(cfg.thermostat.comparison, "below");
    cfg.thermostat.lower_threshold = -10.5f;
    cfg.thermostat.upper_threshold = 25.5f;
    cfg.thermostat.program_id_lower = 3;
    cfg.thermostat.program_id_upper = 4;

    cfg.battery_saver.enabled = true;
    cfg.battery_saver.voltage_start_threshold = 11.9f;
    cfg.battery_saver.voltage_abort_threshold = 10.6f;
    cfg.battery_saver.hysteresis = 0.4f;
    cfg.battery_saver.min_low_voltage_duration_sec = 200;
    cfg.battery_saver.min_time_between_attempts_sec = 1900;
    cfg.battery_saver.max_attempts_per_day = 4;
    cfg.battery_saver.program_id = 2;

    cfg.time.enabled = true;
    strcpy(cfg.time.ntp_server, "pool.ntp.org");
    cfg.time.tz_offset_hours = -5;
    cfg.time.sync_interval_hours = 12;

    cfg.input_triggers_count = 2;
    cfg.input_triggers[0].id = 10;
    cfg.input_triggers[0].enabled = true;
    cfg.input_triggers[0].input_id = 1;
    cfg.input_triggers[0].trigger_level = 1;
    cfg.input_triggers[0].program_id = 5;
    cfg.input_triggers[1].id = 11;
    cfg.input_triggers[1].enabled = false;
    cfg.input_triggers[1].input_id = 0;

    cfg.temperature_triggers_count = 2;
    cfg.temperature_triggers[0].id = 20;
    cfg.temperature_triggers[0].enabled = true;
    cfg.temperature_triggers[0].sensor_id = 2;
    strcpy(cfg.temperature_triggers[0].comparison, "below");
    cfg.temperature_triggers[0].threshold = -12.5f;
    cfg.temperature_triggers[0].program_id = 6;
    cfg.temperature_triggers[1].id = 21;
    cfg.temperature_triggers[1].enabled = false;
    cfg.temperature_triggers[1].sensor_id = 3;
}

static void test_full_config_round_trip_and_seen_sections() {
    BaseConfig cfg;
    fillFullConfig(cfg);

    MemPrint out;
    const size_t n = config_internal::serializeBaseConfigToPrint(cfg, out);
    TEST_ASSERT_EQUAL_UINT32((unsigned)out.s.size(), (unsigned)n);
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"setup_required\":true"));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"ssid\":\"MyCar\""));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"password\":\"pa\\\"ss\""));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"starter_relay_id\":\"K4\""));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"start_input_id\":\"IN1\""));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"engine_detection_source\":\"input\""));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"sensor_name\":\"Cabin\""));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"rom\":\"AA:BB:CC:DD:EE:FF:11:22\""));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"pulses_per_rev\":4"));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"threshold_rpm\":800"));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"input_triggers\":["));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"temperature_triggers\":["));

    BaseConfig back;
    uint16_t seen = 0;
    TEST_ASSERT_TRUE(parseCfgSeen(out.s, back, &seen));
    TEST_ASSERT_EQUAL_UINT16(config_internal::ConfigSection::RequiredPost, seen);
    TEST_ASSERT_TRUE(back.setup_required);
    TEST_ASSERT_EQUAL_STRING("MyCar", back.wifi.ssid);
    TEST_ASSERT_EQUAL_STRING("pa\"ss", back.wifi.password);
    TEST_ASSERT_EQUAL_UINT16(90, back.wifi.ap_timeout_sec);
    TEST_ASSERT_TRUE(back.wifi.ap_timeout_enabled);
    TEST_ASSERT_EQUAL_STRING("+71234567890", back.gsm.owner_phone);
    TEST_ASSERT_EQUAL_STRING("1234", back.gsm.dtmf_password);
    TEST_ASSERT_EQUAL_UINT16(1884, back.mqtt.port);
    TEST_ASSERT_EQUAL_STRING("car/x", back.mqtt.topic_prefix);
    TEST_ASSERT_EQUAL_UINT16(30, back.mqtt.publish_interval_sec);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.012345678f, back.vehicle.adc_voltage_coeff);
    TEST_ASSERT_EQUAL((int)StartMethod::Rpm, (int)back.vehicle.start_method);
    TEST_ASSERT_EQUAL_UINT8(4, back.vehicle.starter_relay_id);
    TEST_ASSERT_EQUAL_UINT8(1, back.vehicle.start_input_id);
    TEST_ASSERT_EQUAL_UINT8(2, back.vehicle.start_block_input_id);
    TEST_ASSERT_EQUAL_STRING("input", back.vehicle.engine_detection_source);
    TEST_ASSERT_EQUAL_UINT8(3, back.vehicle.engine_input_id);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.004f, back.sensors[0].coeff);
    TEST_ASSERT_EQUAL_STRING("Cabin", back.sensors[0].name);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF:11:22", back.sensors[0].rom);
    TEST_ASSERT_EQUAL((int)InputType::PULSE_COUNTER, (int)back.inputs[1].type);
    TEST_ASSERT_EQUAL_UINT16(4, back.inputs[1].pulse.pulses_per_rev);
    TEST_ASSERT_EQUAL_UINT16(800, back.inputs[1].pulse.threshold_rpm);
    TEST_ASSERT_EQUAL_STRING("ACC", back.relays[0].name);
    TEST_ASSERT_EQUAL_STRING("IGN", back.relays[1].name);
    TEST_ASSERT_TRUE(back.thermostat.enabled);
    TEST_ASSERT_EQUAL_UINT8(1, back.thermostat.sensor_id);
    TEST_ASSERT_EQUAL_STRING("below", back.thermostat.comparison);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -10.5f, back.thermostat.lower_threshold);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 25.5f, back.thermostat.upper_threshold);
    TEST_ASSERT_TRUE(back.battery_saver.enabled);
    TEST_ASSERT_EQUAL_UINT16(200, back.battery_saver.min_low_voltage_duration_sec);
    TEST_ASSERT_EQUAL_UINT8(4, back.battery_saver.max_attempts_per_day);
    TEST_ASSERT_TRUE(back.time.enabled);
    TEST_ASSERT_EQUAL_INT8(-5, back.time.tz_offset_hours);
    TEST_ASSERT_EQUAL_UINT32(12, back.time.sync_interval_hours);
    TEST_ASSERT_EQUAL_UINT8(2, back.input_triggers_count);
    TEST_ASSERT_EQUAL_UINT16(10, back.input_triggers[0].id);
    TEST_ASSERT_EQUAL_UINT8(5, back.input_triggers[0].program_id);
    TEST_ASSERT_EQUAL_UINT8(2, back.temperature_triggers_count);
    TEST_ASSERT_EQUAL_UINT16(20, back.temperature_triggers[0].id);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -12.5f, back.temperature_triggers[0].threshold);
}

static void test_crc16_serialized_config_matches_string() {
    BaseConfig cfg;
    fillFullConfig(cfg);
    MemPrint out;
    config_internal::serializeBaseConfigToPrint(cfg, out);

    size_t len = 0;
    const uint16_t crc = config_internal::crc16SerializedBaseConfig(cfg, &len);
    TEST_ASSERT_EQUAL_UINT32((unsigned)out.s.size(), (unsigned)len);
    TEST_ASSERT_EQUAL_HEX16(calculateCRC16(out.s.data(), out.s.size()), crc);
}

static void test_progmem_parse_valid_null_and_invalid() {
    BaseConfig cfg;
    TEST_ASSERT_TRUE(config_internal::parseBaseConfigStreamingFromProgmem(
        cfg, "{\"wifi\":{\"ssid\":\"Progmem\"},\"relays\":[]}"));
    TEST_ASSERT_EQUAL_STRING("Progmem", cfg.wifi.ssid);

    BaseConfig bad;
    TEST_ASSERT_FALSE(config_internal::parseBaseConfigStreamingFromProgmem(bad, nullptr));
    TEST_ASSERT_FALSE(config_internal::parseBaseConfigStreamingFromProgmem(bad, ""));
    TEST_ASSERT_FALSE(config_internal::parseBaseConfigStreamingFromProgmem(bad, "[]"));
    TEST_ASSERT_FALSE(config_internal::parseBaseConfigStreamingFromProgmem(bad, "{\"wifi\":{\"ssid\":\"x\"}"));
}

static void test_seen_sections_partial_and_null_values_ignored() {
    BaseConfig cfg;
    strcpy(cfg.wifi.ssid, "keep");
    uint16_t seen = 0;
    TEST_ASSERT_TRUE(parseCfgSeen("{\"wifi\":{\"ssid\":null,\"ap_timeout_sec\":null},\"relays\":[]}",
                                  cfg, &seen));
    TEST_ASSERT_EQUAL_STRING("keep", cfg.wifi.ssid);
    TEST_ASSERT_EQUAL_UINT16(config_internal::ConfigSection::Wifi | config_internal::ConfigSection::Relays,
                             seen);
}

static void test_strict_root_must_be_object() {
    BaseConfig cfg;
    TEST_ASSERT_FALSE(parseCfg("[]", cfg, config_internal::ParseMode::Strict));
    TEST_ASSERT_FALSE(parseCfg("42", cfg, config_internal::ParseMode::Strict));
    TEST_ASSERT_FALSE(parseCfg("\"text\"", cfg, config_internal::ParseMode::Strict));
    TEST_ASSERT_TRUE(parseCfg("{\"wifi\":null}", cfg, config_internal::ParseMode::Strict));
}

static void test_strict_invalid_numbers_and_silk_fail() {
    BaseConfig cfg;
    TEST_ASSERT_FALSE(parseCfg("{\"wifi\":{\"ap_timeout_sec\":70000}}", cfg,
                               config_internal::ParseMode::Strict));
    TEST_ASSERT_FALSE(parseCfg("{\"vehicle\":{\"starter_relay_id\":\"K9\"}}", cfg,
                               config_internal::ParseMode::Strict));
    TEST_ASSERT_FALSE(parseCfg("{\"battery_saver\":{\"max_attempts_per_day\":999}}", cfg,
                               config_internal::ParseMode::Strict));
}

static void test_mqtt_port_overflow_clamps_to_max() {
    // Текущий контракт: port парсится через parseU16 (clamp), а не tryParseU16 (fail).
    BaseConfig cfg;
    TEST_ASSERT_TRUE(parseCfg("{\"mqtt\":{\"port\":70000}}", cfg,
                              config_internal::ParseMode::Strict));
    TEST_ASSERT_EQUAL_UINT16(65535, cfg.mqtt.port);
}

static void test_serialize_nonfinite_float_as_zero() {
    BaseConfig cfg;
    cfg.vehicle.adc_voltage_coeff = NAN;
    cfg.vehicle.engine_running_voltage_threshold = INFINITY;
    MemPrint out;
    config_internal::serializeBaseConfigToPrint(cfg, out);
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"adc_voltage_coeff\":0,"));
    TEST_ASSERT_NOT_NULL(strstr(out.s.c_str(), "\"engine_running_voltage_threshold\":0,"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_emit_parse_keeps_hidden_fields);
    RUN_TEST(test_adc_calibrate_voltage_is_out_param_only);
    RUN_TEST(test_unknown_keys_and_extra_arrays_trim);
    RUN_TEST(test_digital_active_state_defaults_and_pulse_type);
    RUN_TEST(test_truncated_json_strict_fails_lenient_keeps);
    RUN_TEST(test_program_unclosed_steps_fails);
    RUN_TEST(test_program_empty_action_fails);
    RUN_TEST(test_program_id_zero_fails);
    RUN_TEST(test_program_file_id_must_match_expected);
    RUN_TEST(test_program_step_past_max_fails);
    RUN_TEST(test_program_step_round_trip);
    RUN_TEST(test_battery_saver_u16_overflow_fails_strict);
    RUN_TEST(test_tz_offset_out_of_range_keeps_previous);
    RUN_TEST(test_full_config_round_trip_and_seen_sections);
    RUN_TEST(test_crc16_serialized_config_matches_string);
    RUN_TEST(test_progmem_parse_valid_null_and_invalid);
    RUN_TEST(test_seen_sections_partial_and_null_values_ignored);
    RUN_TEST(test_strict_root_must_be_object);
    RUN_TEST(test_strict_invalid_numbers_and_silk_fail);
    RUN_TEST(test_mqtt_port_overflow_clamps_to_max);
    RUN_TEST(test_serialize_nonfinite_float_as_zero);
    return UNITY_END();
}
