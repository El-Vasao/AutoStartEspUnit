#include <unity.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "program/ProgramValidate.h"

using program_validate::ChannelRef;
using program_validate::ChannelTables;
using program_validate::checkStep;
using program_validate::Issue;

static constexpr uint8_t kStarter = 2; // K2
static const uint16_t kInTrig[] = {10, 11};
static const uint16_t kTempTrig[] = {20, 21};

static ChannelTables tables() {
    ChannelTables t;
    t.relayCount = 5;
    t.inputCount = 3;
    t.inputTriggers = {kInTrig, 2};
    t.tempTriggers = {kTempTrig, 2};
    return t;
}

static Issue ck(ActionId action, ChannelRef ch, uint8_t starter = kStarter) {
    return checkStep(action, ch, starter, tables());
}

void setUp() {}
void tearDown() {}

static void test_unknown_action_is_rejected() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::UnknownAction, (uint8_t)ck(ActionId::UNKNOWN, {}));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::UnknownAction,
                            (uint8_t)ck(actionIdFromString("NOPE"), {}));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::UnknownAction,
                            (uint8_t)ck(actionIdFromString(""), {}));
}

static void test_relay_steps_on_starter_relay_are_rejected() {
    const ActionId relayActions[] = {ActionId::RELAY_ON, ActionId::RELAY_OFF, ActionId::RELAY_TOGGLE,
                                     ActionId::RELAY_PULSE_ON_OFF_ON, ActionId::RELAY_PULSE_OFF_ON_OFF};
    for (ActionId a : relayActions) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::DrivesStarterRelay,
                                (uint8_t)ck(a, ChannelRef{kStarter, 0, 0, 0}));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::None, (uint8_t)ck(a, ChannelRef{1, 0, 0, 0}));
    }
}

static void test_starter_rule_needs_assigned_starter() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::None, (uint8_t)ck(ActionId::RELAY_ON, ChannelRef{1, 0, 0, 0}, 0));
}

static void test_relay_unknown_id_is_invalid_ref() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::InvalidRef, (uint8_t)ck(ActionId::RELAY_ON, ChannelRef{0, 0, 0, 0}));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::InvalidRef, (uint8_t)ck(ActionId::RELAY_OFF, ChannelRef{9, 0, 0, 0}));
}

static void test_input_and_trigger_refs() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::None,
                            (uint8_t)ck(ActionId::INPUT_ENABLE, ChannelRef{0, 1, 0, 0}));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::InvalidRef,
                            (uint8_t)ck(ActionId::INPUT_DISABLE, ChannelRef{0, 0, 0, 0}));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::InvalidRef,
                            (uint8_t)ck(ActionId::INPUT_ENABLE, ChannelRef{0, 9, 0, 0}));

    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::None,
                            (uint8_t)ck(ActionId::INPUT_TRIGGER_ENABLE, ChannelRef{0, 0, 10, 0}));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::InvalidRef,
                            (uint8_t)ck(ActionId::INPUT_TRIGGER_DISABLE, ChannelRef{0, 0, 99, 0}));

    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::None,
                            (uint8_t)ck(ActionId::TEMP_TRIGGER_ENABLE, ChannelRef{0, 0, 0, 20}));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::InvalidRef,
                            (uint8_t)ck(ActionId::TEMP_TRIGGER_DISABLE, ChannelRef{0, 0, 0, 0}));
}

static void test_non_relay_actions_ignore_relay_id() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::None, (uint8_t)ck(ActionId::ENGINE_START, ChannelRef{kStarter, 0, 0, 0}));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::None, (uint8_t)ck(ActionId::WAIT, ChannelRef{kStarter, 0, 0, 0}));
}

static void test_sms_owner_requires_message() {
    ChannelRef emptyMsg{0, 0, 0, 0, ""};
    ChannelRef noMsg{0, 0, 0, 0, nullptr};
    ChannelRef okMsg{0, 0, 0, 0, "hi"};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::EmptyMessage, (uint8_t)ck(ActionId::SMS_OWNER, emptyMsg));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::EmptyMessage, (uint8_t)ck(ActionId::SMS_OWNER, noMsg));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::None, (uint8_t)ck(ActionId::SMS_OWNER, okMsg));
}

static int countActions(const std::string& text, int* unknown) {
    int actions = 0;
    size_t pos = 0;
    while ((pos = text.find("\"action\"", pos)) != std::string::npos) {
        const size_t colon = text.find(':', pos);
        const size_t q1 = text.find('"', colon + 1);
        const size_t q2 = text.find('"', q1 + 1);
        TEST_ASSERT_TRUE(q2 != std::string::npos);
        const std::string name = text.substr(q1 + 1, q2 - q1 - 1);
        if (actionIdFromString(name.c_str()) == ActionId::UNKNOWN) (*unknown)++;
        actions++;
        pos = q2 + 1;
    }
    return actions;
}

static void test_shared_program_fixtures_match_action_ids() {
    const std::filesystem::path dir("test/fixtures/programs");
    TEST_ASSERT_TRUE_MESSAGE(std::filesystem::is_directory(dir), "test/fixtures/programs");
    int files = 0;
    for (const auto& ent : std::filesystem::directory_iterator(dir)) {
        if (!ent.is_regular_file() || ent.path().extension() != ".json") continue;
        if (ent.path().filename() == "index.json") continue;
        std::ifstream in(ent.path());
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        int unknown = 0;
        const int actions = countActions(text, &unknown);
        TEST_ASSERT_TRUE_MESSAGE(actions > 0, ent.path().filename().string().c_str());
        const bool expectUnknown = ent.path().filename() == "unknown-action.json";
        TEST_ASSERT_EQUAL_MESSAGE(expectUnknown ? 1 : 0, unknown, ent.path().filename().string().c_str());
        files++;
    }
    TEST_ASSERT_TRUE(files >= 2);
}

static void test_issue_names() {
    TEST_ASSERT_EQUAL_STRING("", program_validate::issueToString(Issue::None));
    TEST_ASSERT_EQUAL_STRING("unknown action", program_validate::issueToString(Issue::UnknownAction));
    TEST_ASSERT_EQUAL_STRING("drives the starter relay", program_validate::issueToString(Issue::DrivesStarterRelay));
    TEST_ASSERT_EQUAL_STRING("invalid channel reference", program_validate::issueToString(Issue::InvalidRef));
    TEST_ASSERT_EQUAL_STRING("empty SMS message", program_validate::issueToString(Issue::EmptyMessage));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_unknown_action_is_rejected);
    RUN_TEST(test_relay_steps_on_starter_relay_are_rejected);
    RUN_TEST(test_starter_rule_needs_assigned_starter);
    RUN_TEST(test_relay_unknown_id_is_invalid_ref);
    RUN_TEST(test_input_and_trigger_refs);
    RUN_TEST(test_non_relay_actions_ignore_relay_id);
    RUN_TEST(test_sms_owner_requires_message);
    RUN_TEST(test_shared_program_fixtures_match_action_ids);
    RUN_TEST(test_issue_names);
    return UNITY_END();
}
