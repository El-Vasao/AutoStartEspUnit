#include <unity.h>

#include <string.h>

#include "HostFakes.h"
#include "common/Pins.h"
#include "config/Config.h"
#include "core/Core.h"
#include "io/RelayController.h"
#include "program/ProgramExecutor.h"

static ProgramExecutor& exec() { return core.getProgramExecutor(); }
static RelayController& relays() { return core.getRelay(); }

static CompiledStep step(ActionId action, uint8_t relayId, uint32_t ms) {
    CompiledStep s;
    s.action = action;
    s.relay_id = relayId;
    s.ms = ms;
    return s;
}

void setUp() {
    test_clock::set(5000);
    test_gpio::reset();
    ProgramExecutorTestFlags::overrideStarter = false;
    ExecutorTestProbe::intercept = false;
    ProgramTestFeed::reset();
    relays().begin();
    relays().allOff();
    exec().stop();
}

void tearDown() { relays().allOff(); }

static void test_steps_run_in_order_then_finish() {
    const CompiledStep steps[] = {
        step(ActionId::RELAY_ON, 1, 0),
        step(ActionId::WAIT, 0, 1000),
        step(ActionId::RELAY_OFF, 1, 0),
    };
    ProgramTestFeed::set(steps, 3, "run");
    TEST_ASSERT_TRUE(exec().start(3));
    TEST_ASSERT_TRUE(exec().isRunning());
    TEST_ASSERT_TRUE(relays().getState(0));
    TEST_ASSERT_EQUAL_UINT8(1, exec().getCurrentStep());

    test_clock::advance(999);
    exec().update();
    TEST_ASSERT_TRUE(exec().isRunning());
    TEST_ASSERT_TRUE(relays().getState(0));

    test_clock::advance(1);
    exec().update();
    TEST_ASSERT_FALSE(exec().isRunning());
    TEST_ASSERT_FALSE(relays().getState(0));
    TEST_ASSERT_EQUAL_UINT8(3, exec().getLastProgramId());
}

static void test_stop_turns_every_relay_off() {
    const CompiledStep steps[] = {
        step(ActionId::RELAY_ON, 1, 0),
        step(ActionId::RELAY_ON, 2, 0),
        step(ActionId::WAIT, 0, 60000),
    };
    ProgramTestFeed::set(steps, 3, "hold");
    TEST_ASSERT_TRUE(exec().start(1));
    TEST_ASSERT_TRUE(relays().getState(0));
    TEST_ASSERT_TRUE(relays().getState(1));

    exec().stop();
    TEST_ASSERT_FALSE(exec().isRunning());
    TEST_ASSERT_EQUAL_UINT8(0, relays().getMask());
}

static void test_new_run_preempts_and_drops_only_starter() {
    const CompiledStep first[] = {
        step(ActionId::RELAY_ON, 1, 0),
        step(ActionId::RELAY_ON, 2, 0),
        step(ActionId::WAIT, 0, 60000),
    };
    ProgramTestFeed::set(first, 3, "first");
    TEST_ASSERT_TRUE(exec().start(1));
    TEST_ASSERT_EQUAL_INT8(2, relays().starterIndex());
    TEST_ASSERT_TRUE(relays().on(2));
    TEST_ASSERT_EQUAL_UINT8(0x07, relays().getMask());

    const CompiledStep second[] = {
        step(ActionId::RELAY_ON, 1, 0),
        step(ActionId::WAIT, 0, 60000),
    };
    ProgramTestFeed::set(second, 2, "second");
    TEST_ASSERT_TRUE(exec().start(4));
    TEST_ASSERT_TRUE(exec().isRunning());
    TEST_ASSERT_EQUAL_UINT8(4, exec().getCurrentProgramId());
    TEST_ASSERT_TRUE(relays().getState(0));
    TEST_ASSERT_TRUE(relays().getState(1));
    TEST_ASSERT_FALSE(relays().getState(2));
}

static void test_missing_program_stops_outputs() {
    const CompiledStep steps[] = {step(ActionId::RELAY_ON, 1, 0), step(ActionId::WAIT, 0, 60000)};
    ProgramTestFeed::set(steps, 2, "live");
    TEST_ASSERT_TRUE(exec().start(1));
    ProgramTestFeed::reset();
    TEST_ASSERT_FALSE(exec().start(9));
    TEST_ASSERT_FALSE(exec().isRunning());
    TEST_ASSERT_EQUAL_UINT8(0, relays().getMask());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_steps_run_in_order_then_finish);
    RUN_TEST(test_stop_turns_every_relay_off);
    RUN_TEST(test_new_run_preempts_and_drops_only_starter);
    RUN_TEST(test_missing_program_stops_outputs);
    return UNITY_END();
}
