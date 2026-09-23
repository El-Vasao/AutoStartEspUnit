#include "program/ProgramAction.h"

#include <string.h>

ActionId actionIdFromString(const char* action) {
    if (!action || !*action) return ActionId::UNKNOWN;

    if (strcmp(action, "WAIT") == 0) return ActionId::WAIT;
    if (strcmp(action, "RELAY_PULSE_ON_OFF_ON") == 0) return ActionId::RELAY_PULSE_ON_OFF_ON;
    if (strcmp(action, "RELAY_PULSE_OFF_ON_OFF") == 0) return ActionId::RELAY_PULSE_OFF_ON_OFF;
    if (strcmp(action, "STARTER_TIMED") == 0) return ActionId::STARTER_TIMED;
    if (strcmp(action, "STARTER_WAIT_INPUT") == 0) return ActionId::STARTER_WAIT_INPUT;

    if (strcmp(action, "RELAY_ON") == 0) return ActionId::RELAY_ON;
    if (strcmp(action, "RELAY_OFF") == 0) return ActionId::RELAY_OFF;
    if (strcmp(action, "RELAY_TOGGLE") == 0) return ActionId::RELAY_TOGGLE;

    if (strcmp(action, "INPUT_ENABLE") == 0) return ActionId::INPUT_ENABLE;
    if (strcmp(action, "INPUT_DISABLE") == 0) return ActionId::INPUT_DISABLE;
    if (strcmp(action, "INPUT_TRIGGER_ENABLE") == 0) return ActionId::INPUT_TRIGGER_ENABLE;
    if (strcmp(action, "INPUT_TRIGGER_DISABLE") == 0) return ActionId::INPUT_TRIGGER_DISABLE;
    if (strcmp(action, "TEMP_TRIGGER_ENABLE") == 0) return ActionId::TEMP_TRIGGER_ENABLE;
    if (strcmp(action, "TEMP_TRIGGER_DISABLE") == 0) return ActionId::TEMP_TRIGGER_DISABLE;

    if (strcmp(action, "BATTERY_SAVER_ON") == 0) return ActionId::BATTERY_SAVER_ON;
    if (strcmp(action, "BATTERY_SAVER_OFF") == 0) return ActionId::BATTERY_SAVER_OFF;
    if (strcmp(action, "THERMOSTAT_ON") == 0) return ActionId::THERMOSTAT_ON;
    if (strcmp(action, "THERMOSTAT_OFF") == 0) return ActionId::THERMOSTAT_OFF;

    return ActionId::UNKNOWN;
}

const char* actionIdToString(ActionId id) {
    switch (id) {
        case ActionId::WAIT: return "WAIT";
        case ActionId::RELAY_PULSE_ON_OFF_ON: return "RELAY_PULSE_ON_OFF_ON";
        case ActionId::RELAY_PULSE_OFF_ON_OFF: return "RELAY_PULSE_OFF_ON_OFF";
        case ActionId::STARTER_TIMED: return "STARTER_TIMED";
        case ActionId::STARTER_WAIT_INPUT: return "STARTER_WAIT_INPUT";
        case ActionId::RELAY_ON: return "RELAY_ON";
        case ActionId::RELAY_OFF: return "RELAY_OFF";
        case ActionId::RELAY_TOGGLE: return "RELAY_TOGGLE";
        case ActionId::INPUT_ENABLE: return "INPUT_ENABLE";
        case ActionId::INPUT_DISABLE: return "INPUT_DISABLE";
        case ActionId::INPUT_TRIGGER_ENABLE: return "INPUT_TRIGGER_ENABLE";
        case ActionId::INPUT_TRIGGER_DISABLE: return "INPUT_TRIGGER_DISABLE";
        case ActionId::TEMP_TRIGGER_ENABLE: return "TEMP_TRIGGER_ENABLE";
        case ActionId::TEMP_TRIGGER_DISABLE: return "TEMP_TRIGGER_DISABLE";
        case ActionId::BATTERY_SAVER_ON: return "BATTERY_SAVER_ON";
        case ActionId::BATTERY_SAVER_OFF: return "BATTERY_SAVER_OFF";
        case ActionId::THERMOSTAT_ON: return "THERMOSTAT_ON";
        case ActionId::THERMOSTAT_OFF: return "THERMOSTAT_OFF";
        case ActionId::UNKNOWN:
        default:
            return "";
    }
}
