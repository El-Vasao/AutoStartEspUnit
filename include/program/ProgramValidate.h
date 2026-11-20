// include/program/ProgramValidate.h
#pragma once

#include <stdint.h>

#include "program/ProgramAction.h"

/**
 * Проверка шагов программы — общая для приёма программы (Config::commitPostedProgramFile)
 * и запуска (ProgramExecutor::start). Без Arduino, тестируется на хосте. См. docs/modules/program.md.
 *
 * relay_id / input_id — номер шёлка 1…n (K1 / IN1), 0 = нет.
 */
namespace program_validate {

enum class Issue : uint8_t {
    None,
    /// Действие не распознано (строка отсутствует в таблице ActionId).
    UnknownAction,
    /// Шаг RELAY_* адресует реле стартера: стартером управляет только ENGINE_START.
    DrivesStarterRelay,
    /// input_id / relay_id / trigger id нет на устройстве.
    InvalidRef,
    /// SMS_OWNER с пустым message.
    EmptyMessage,
};

struct IdTable {
    const uint16_t* ids{nullptr};
    uint8_t count{0};
};

struct ChannelRef {
    uint8_t relay_id{0};
    uint8_t input_id{0};
    uint16_t input_trigger_id{0};
    uint16_t temp_trigger_id{0};
    const char* message{nullptr}; ///< для SMS_OWNER; иначе nullptr
};

struct ChannelTables {
    uint8_t relayCount{0};  ///< допустимы relay_id 1…relayCount
    uint8_t inputCount{0};  ///< допустимы input_id 1…inputCount
    IdTable inputTriggers{};
    IdTable tempTriggers{};
};

/// `starterRelayNumber` = 0 — реле стартера не назначено; иначе номер K1… (1…n).
Issue checkStep(ActionId action, const ChannelRef& ch, uint8_t starterRelayNumber, const ChannelTables& tables);

/// Короткое имя проблемы для журнала ("unknown action", ...); "" для None.
const char* issueToString(Issue issue);

} // namespace program_validate
