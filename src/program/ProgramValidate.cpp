// src/program/ProgramValidate.cpp
#include "program/ProgramValidate.h"

namespace program_validate {

namespace {

bool containsId(IdTable table, uint16_t id) {
    if (id == 0 || !table.ids) return false;
    for (uint8_t i = 0; i < table.count; i++) {
        if (table.ids[i] == id) return true;
    }
    return false;
}

bool numberInRange(uint8_t number, uint8_t count) {
    return number >= 1 && number <= count;
}

} // namespace

Issue checkStep(ActionId action, const ChannelRef& ch, uint8_t starterRelayNumber, const ChannelTables& tables) {
    if (action == ActionId::UNKNOWN) return Issue::UnknownAction;
    if (isRelayAction(action)) {
        if (!numberInRange(ch.relay_id, tables.relayCount)) return Issue::InvalidRef;
        if (starterRelayNumber != 0 && ch.relay_id == starterRelayNumber) {
            return Issue::DrivesStarterRelay;
        }
    }
    if (action == ActionId::INPUT_ENABLE || action == ActionId::INPUT_DISABLE) {
        if (!numberInRange(ch.input_id, tables.inputCount)) return Issue::InvalidRef;
    }
    if (action == ActionId::INPUT_TRIGGER_ENABLE || action == ActionId::INPUT_TRIGGER_DISABLE) {
        if (!containsId(tables.inputTriggers, ch.input_trigger_id)) return Issue::InvalidRef;
    }
    if (action == ActionId::TEMP_TRIGGER_ENABLE || action == ActionId::TEMP_TRIGGER_DISABLE) {
        if (!containsId(tables.tempTriggers, ch.temp_trigger_id)) return Issue::InvalidRef;
    }
    if (action == ActionId::SMS_OWNER) {
        if (!ch.message || !ch.message[0]) return Issue::EmptyMessage;
    }
    return Issue::None;
}

const char* issueToString(Issue issue) {
    switch (issue) {
        case Issue::UnknownAction:      return "unknown action";
        case Issue::DrivesStarterRelay: return "drives the starter relay";
        case Issue::InvalidRef:         return "invalid channel reference";
        case Issue::EmptyMessage:       return "empty SMS message";
        case Issue::None:               break;
    }
    return "";
}

} // namespace program_validate
