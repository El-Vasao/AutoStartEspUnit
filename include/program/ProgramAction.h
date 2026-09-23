#pragma once

#include <stdint.h>

/// Compiled action id for program execution (no strings in hot-path).
enum class ActionId : uint8_t {
    UNKNOWN = 0,

    // Timed / stateful
    WAIT,
    RELAY_PULSE_ON_OFF_ON,
    RELAY_PULSE_OFF_ON_OFF,
    STARTER_TIMED,
    STARTER_WAIT_INPUT,

    // Instant
    RELAY_ON,
    RELAY_OFF,
    RELAY_TOGGLE,
    INPUT_ENABLE,
    INPUT_DISABLE,
    INPUT_TRIGGER_ENABLE,
    INPUT_TRIGGER_DISABLE,
    TEMP_TRIGGER_ENABLE,
    TEMP_TRIGGER_DISABLE,
    BATTERY_SAVER_ON,
    BATTERY_SAVER_OFF,
    THERMOSTAT_ON,
    THERMOSTAT_OFF,
};

/// Temperature comparison operator in compiled steps.
enum class ComparisonOp : uint8_t {
    Above = 0,
    Below = 1,
};

/// Single source for ActionId ↔ JSON/program string mapping.
ActionId actionIdFromString(const char* action);
/// Returns canonical action name, or "" for UNKNOWN / invalid.
const char* actionIdToString(ActionId id);

