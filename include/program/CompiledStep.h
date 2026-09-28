#pragma once

#include <stdint.h>
#include "common/Constants.h"
#include "program/ProgramAction.h"
/**
 * @brief Compact representation of a program step for runtime execution.
 *
 * Important (ESP8266):
 * - no strings, no heap
 * - POD, fixed-size
 */
struct CompiledStep {
    ActionId action{ActionId::UNKNOWN};

    uint16_t relay_id{0};
    uint32_t ms{0};
    uint32_t timeout_ms{0};

    uint16_t input_id{0};
    uint16_t sensor_id{0};
    uint16_t input_trigger_id{0};
    uint16_t temp_trigger_id{0};
    uint8_t program_id{0};

    uint8_t retries{1};
    uint8_t expected_state{1};
    ComparisonOp comparison{ComparisonOp::Above};
    float threshold{0.0f};
    uint8_t engine_state{1};
    uint8_t timeout_action{0};
    uint8_t skip_count{0};
    /// SMS_OWNER text (POD fixed buffer).
    char message[TextBytes::Programs::STEP_MESSAGE]{};
};

