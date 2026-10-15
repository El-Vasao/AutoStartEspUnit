// src/program/ProgramValidate.cpp
#include "program/ProgramValidate.h"

namespace program_validate {

Issue checkStep(ActionId action, uint16_t relayId, uint16_t starterRelayId) {
    if (action == ActionId::UNKNOWN) return Issue::UnknownAction;
    if (isRelayAction(action) && starterRelayId != 0 && relayId == starterRelayId) {
        return Issue::DrivesStarterRelay;
    }
    return Issue::None;
}

const char* issueToString(Issue issue) {
    switch (issue) {
        case Issue::UnknownAction:      return "unknown action";
        case Issue::DrivesStarterRelay: return "drives the starter relay";
        case Issue::None:               break;
    }
    return "";
}

} // namespace program_validate
