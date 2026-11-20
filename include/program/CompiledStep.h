#pragma once

#include <stdint.h>
#include "common/Constants.h"
#include "program/ProgramAction.h"
/**
 * Компактный шаг программы для исполнения: POD, без heap, без строк кроме текста SMS.
 * relay_id / input_id — номер шёлка 1…n (K1 / IN1), 0 = нет.
 */
struct CompiledStep {
    ActionId action{ActionId::UNKNOWN};

    uint8_t relay_id{0};
    uint32_t ms{0};

    uint8_t input_id{0};
    uint16_t input_trigger_id{0};
    uint16_t temp_trigger_id{0};

    /// Текст SMS_OWNER (фиксированный POD-буфер).
    char message[TextBytes::Programs::STEP_MESSAGE]{};
};
