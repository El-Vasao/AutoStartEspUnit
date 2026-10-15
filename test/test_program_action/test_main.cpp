#include <string.h>
#include <unity.h>

#include "program/ProgramAction.h"

static void assertRoundTrip(const char* name, ActionId id) {
    TEST_ASSERT_EQUAL((int)id, (int)actionIdFromString(name));
    TEST_ASSERT_EQUAL_STRING(name, actionIdToString(id));
}

void test_known_actions_round_trip() {
    assertRoundTrip("WAIT", ActionId::WAIT);
    assertRoundTrip("RELAY_PULSE_ON_OFF_ON", ActionId::RELAY_PULSE_ON_OFF_ON);
    assertRoundTrip("RELAY_PULSE_OFF_ON_OFF", ActionId::RELAY_PULSE_OFF_ON_OFF);
    assertRoundTrip("ENGINE_START", ActionId::ENGINE_START);
    assertRoundTrip("RELAY_ON", ActionId::RELAY_ON);
    assertRoundTrip("RELAY_OFF", ActionId::RELAY_OFF);
    assertRoundTrip("RELAY_TOGGLE", ActionId::RELAY_TOGGLE);
    assertRoundTrip("INPUT_ENABLE", ActionId::INPUT_ENABLE);
    assertRoundTrip("INPUT_DISABLE", ActionId::INPUT_DISABLE);
    assertRoundTrip("INPUT_TRIGGER_ENABLE", ActionId::INPUT_TRIGGER_ENABLE);
    assertRoundTrip("INPUT_TRIGGER_DISABLE", ActionId::INPUT_TRIGGER_DISABLE);
    assertRoundTrip("TEMP_TRIGGER_ENABLE", ActionId::TEMP_TRIGGER_ENABLE);
    assertRoundTrip("TEMP_TRIGGER_DISABLE", ActionId::TEMP_TRIGGER_DISABLE);
    assertRoundTrip("BATTERY_SAVER_ON", ActionId::BATTERY_SAVER_ON);
    assertRoundTrip("BATTERY_SAVER_OFF", ActionId::BATTERY_SAVER_OFF);
    assertRoundTrip("THERMOSTAT_ON", ActionId::THERMOSTAT_ON);
    assertRoundTrip("THERMOSTAT_OFF", ActionId::THERMOSTAT_OFF);
    assertRoundTrip("CALL_OWNER", ActionId::CALL_OWNER);
    assertRoundTrip("SMS_OWNER", ActionId::SMS_OWNER);
}

void test_unknown_action() {
    TEST_ASSERT_EQUAL((int)ActionId::UNKNOWN, (int)actionIdFromString("NOPE"));
    TEST_ASSERT_EQUAL((int)ActionId::UNKNOWN, (int)actionIdFromString(""));
    TEST_ASSERT_EQUAL((int)ActionId::UNKNOWN, (int)actionIdFromString(nullptr));
    TEST_ASSERT_EQUAL_STRING("", actionIdToString(ActionId::UNKNOWN));
}

void test_unknown_strings_are_unknown() {
    TEST_ASSERT_EQUAL((int)ActionId::UNKNOWN, (int)actionIdFromString("NOT_AN_ACTION"));
    TEST_ASSERT_EQUAL((int)ActionId::UNKNOWN, (int)actionIdFromString("wait"));
}

void test_action_classes() {
    TEST_ASSERT_TRUE(isStarterAction(ActionId::ENGINE_START));
    TEST_ASSERT_FALSE(isStarterAction(ActionId::RELAY_ON));
    TEST_ASSERT_FALSE(isStarterAction(ActionId::WAIT));
    TEST_ASSERT_TRUE(isRelayAction(ActionId::RELAY_ON));
    TEST_ASSERT_TRUE(isRelayAction(ActionId::RELAY_OFF));
    TEST_ASSERT_TRUE(isRelayAction(ActionId::RELAY_TOGGLE));
    TEST_ASSERT_TRUE(isRelayAction(ActionId::RELAY_PULSE_ON_OFF_ON));
    TEST_ASSERT_TRUE(isRelayAction(ActionId::RELAY_PULSE_OFF_ON_OFF));
    TEST_ASSERT_FALSE(isRelayAction(ActionId::ENGINE_START));
}

void test_case_sensitive() {
    TEST_ASSERT_EQUAL((int)ActionId::UNKNOWN, (int)actionIdFromString("wait"));
    TEST_ASSERT_EQUAL((int)ActionId::UNKNOWN, (int)actionIdFromString("Relay_On"));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_known_actions_round_trip);
    RUN_TEST(test_unknown_action);
    RUN_TEST(test_unknown_strings_are_unknown);
    RUN_TEST(test_action_classes);
    RUN_TEST(test_case_sensitive);
    return UNITY_END();
}
