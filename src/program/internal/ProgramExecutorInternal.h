#pragma once

#include "config/Config.h"
#include "common/Logger.h"

namespace program_executor_internal {
static inline bool validTriggerIndex(uint8_t idx) {
    return idx < Limits::MAX_TRIGGERS;
}

static inline int8_t findInputTriggerIndexById(uint16_t id) {
    if (!id) return -1;
    const auto& full = config.getBase();
    for (uint8_t i = 0; i < full.input_triggers_count; i++) {
        if (full.input_triggers[i].id == id) return (int8_t)i;
    }
    return -1;
}

static inline int8_t findTempTriggerIndexById(uint16_t id) {
    if (!id) return -1;
    const auto& full = config.getBase();
    for (uint8_t i = 0; i < full.temperature_triggers_count; i++) {
        if (full.temperature_triggers[i].id == id) return (int8_t)i;
    }
    return -1;
}

static inline void logInvalidTriggerAndSkip(uint8_t idx, const char* what) {
    logger.log(LogModule::ProgramExecutor, "Invalid %s index %u (max=%u), skipping step\n",
               what ? what : "trigger", (unsigned)idx, (unsigned)Limits::MAX_TRIGGERS);
}
} // namespace program_executor_internal

