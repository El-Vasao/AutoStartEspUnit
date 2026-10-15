#pragma once

#include <stdint.h>
#include "common/Constants.h"
#include "program/ProgramAction.h"
/**
 * Compact program step for runtime execution: POD, no heap, no strings except SMS text.
 */
struct CompiledStep {
    ActionId action{ActionId::UNKNOWN};

    uint16_t relay_id{0};
    uint32_t ms{0};

    uint16_t input_id{0};
    uint16_t input_trigger_id{0};
    uint16_t temp_trigger_id{0};

    /// SMS_OWNER text (POD fixed buffer).
    char message[TextBytes::Programs::STEP_MESSAGE]{};
};
