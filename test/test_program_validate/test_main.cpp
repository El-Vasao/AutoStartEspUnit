#include <unity.h>

#include "program/ProgramValidate.h"

using program_validate::checkStep;
using program_validate::Issue;

static constexpr uint16_t kStarter = 2002;

void setUp() {}
void tearDown() {}

static void test_unknown_action_is_rejected() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::UnknownAction, (uint8_t)checkStep(ActionId::UNKNOWN, 0, kStarter));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::UnknownAction,
                            (uint8_t)checkStep(actionIdFromString("NOPE"), 0, kStarter));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::UnknownAction,
                            (uint8_t)checkStep(actionIdFromString(""), 0, kStarter));
}

static void test_relay_steps_on_starter_relay_are_rejected() {
    const ActionId relayActions[] = {ActionId::RELAY_ON, ActionId::RELAY_OFF, ActionId::RELAY_TOGGLE,
                                     ActionId::RELAY_PULSE_ON_OFF_ON, ActionId::RELAY_PULSE_OFF_ON_OFF};
    for (ActionId a : relayActions) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::DrivesStarterRelay, (uint8_t)checkStep(a, kStarter, kStarter));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::None, (uint8_t)checkStep(a, 2001, kStarter));
    }
}

static void test_starter_rule_needs_assigned_starter() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::None, (uint8_t)checkStep(ActionId::RELAY_ON, 0, 0));
}

static void test_non_relay_actions_ignore_relay_id() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::None, (uint8_t)checkStep(ActionId::ENGINE_START, kStarter, kStarter));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Issue::None, (uint8_t)checkStep(ActionId::WAIT, kStarter, kStarter));
}

static void test_issue_names() {
    TEST_ASSERT_EQUAL_STRING("", program_validate::issueToString(Issue::None));
    TEST_ASSERT_EQUAL_STRING("unknown action", program_validate::issueToString(Issue::UnknownAction));
    TEST_ASSERT_EQUAL_STRING("drives the starter relay", program_validate::issueToString(Issue::DrivesStarterRelay));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_unknown_action_is_rejected);
    RUN_TEST(test_relay_steps_on_starter_relay_are_rejected);
    RUN_TEST(test_starter_rule_needs_assigned_starter);
    RUN_TEST(test_non_relay_actions_ignore_relay_id);
    RUN_TEST(test_issue_names);
    return UNITY_END();
}
