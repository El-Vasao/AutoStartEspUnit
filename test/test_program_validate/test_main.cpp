#include <unity.h>

#include "program/ProgramValidate.h"

using program_validate::ChannelRef;
using program_validate::ChannelTables;
using program_validate::checkStep;
using program_validate::Issue;

static constexpr uint16_t kStarter = 2002;
static const uint16_t kRelays[] = {2001, 2002, 2003, 2004, 2005};
static const uint16_t kInputs[] = {1001, 1002, 1003};
static const uint16_t kInTrig[] = {10, 11};
static const uint16_t kTempTrig[] = {20, 21};

static ChannelTables tables() {
    ChannelTables t;
    t.relays = {kRelays, 5};
    t.inputs = {kInputs, 3};
    t.inputTriggers = {kInTrig, 2};
    t.tempTriggers = {kTempTrig, 2};
    return t;
}

static Issue ck(ActionId action, ChannelRef ch, uint16_t starter = kStarter) {
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
        TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::None, (uint8_t)ck(a, ChannelRef{2001, 0, 0, 0}));
    }
}

static void test_starter_rule_needs_assigned_starter() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::None, (uint8_t)ck(ActionId::RELAY_ON, ChannelRef{2001, 0, 0, 0}, 0));
}

static void test_relay_unknown_id_is_invalid_ref() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::InvalidRef, (uint8_t)ck(ActionId::RELAY_ON, ChannelRef{0, 0, 0, 0}));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::InvalidRef, (uint8_t)ck(ActionId::RELAY_OFF, ChannelRef{9999, 0, 0, 0}));
}

static void test_input_and_trigger_refs() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::None,
                            (uint8_t)ck(ActionId::INPUT_ENABLE, ChannelRef{0, 1001, 0, 0}));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::InvalidRef,
                            (uint8_t)ck(ActionId::INPUT_DISABLE, ChannelRef{0, 0, 0, 0}));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::InvalidRef,
                            (uint8_t)ck(ActionId::INPUT_ENABLE, ChannelRef{0, 1999, 0, 0}));

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

static void test_issue_names() {
    TEST_ASSERT_EQUAL_STRING("", program_validate::issueToString(Issue::None));
    TEST_ASSERT_EQUAL_STRING("unknown action", program_validate::issueToString(Issue::UnknownAction));
    TEST_ASSERT_EQUAL_STRING("drives the starter relay", program_validate::issueToString(Issue::DrivesStarterRelay));
    TEST_ASSERT_EQUAL_STRING("invalid channel reference", program_validate::issueToString(Issue::InvalidRef));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_unknown_action_is_rejected);
    RUN_TEST(test_relay_steps_on_starter_relay_are_rejected);
    RUN_TEST(test_starter_rule_needs_assigned_starter);
    RUN_TEST(test_relay_unknown_id_is_invalid_ref);
    RUN_TEST(test_input_and_trigger_refs);
    RUN_TEST(test_non_relay_actions_ignore_relay_id);
    RUN_TEST(test_issue_names);
    return UNITY_END();
}
