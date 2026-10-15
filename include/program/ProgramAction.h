#pragma once

#include <stdint.h>

/// Compiled action id for program execution (no strings in hot-path).
enum class ActionId : uint8_t {
    UNKNOWN = 0,

    // Timed / stateful
    WAIT,
    RELAY_PULSE_ON_OFF_ON,
    RELAY_PULSE_OFF_ON_OFF,
    /// Engine start: algorithm, input, timings and attempts come from config.vehicle.
    ENGINE_START,

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

    /// Dial config.gsm.owner_phone (voice); wait `ms` then hang up.
    CALL_OWNER,
    /// SMS to owner_phone with step `message` text.
    SMS_OWNER,
};

/// Single source for ActionId ↔ JSON/program string mapping.
ActionId actionIdFromString(const char* action);
/// Returns canonical action name, or "" for UNKNOWN / invalid.
const char* actionIdToString(ActionId id);

/// Step drives the starter relay (guards, battery saver, preemption rules).
bool isStarterAction(ActionId id);

/// Step addresses a relay through `relay_id` (RELAY_ON/OFF/TOGGLE/PULSE_*).
bool isRelayAction(ActionId id);

