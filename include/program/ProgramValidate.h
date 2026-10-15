// include/program/ProgramValidate.h
#pragma once

#include <stdint.h>

#include "program/ProgramAction.h"

/**
 * Проверка шагов программы — общая для приёма программы (Config::commitPostedProgramFile)
 * и запуска (ProgramExecutor::start). Без Arduino, тестируется на хосте. См. docs/modules/program.md.
 */
namespace program_validate {

enum class Issue : uint8_t {
    None,
    /// Действие не распознано (строка отсутствует в таблице ActionId).
    UnknownAction,
    /// Шаг RELAY_* адресует реле стартера: стартером управляет только ENGINE_START.
    DrivesStarterRelay,
    /// input_id / relay_id / trigger id нет в таблице устройства.
    InvalidRef,
};

struct IdTable {
    const uint16_t* ids{nullptr};
    uint8_t count{0};
};

struct ChannelRef {
    uint16_t relay_id{0};
    uint16_t input_id{0};
    uint16_t input_trigger_id{0};
    uint16_t temp_trigger_id{0};
};

struct ChannelTables {
    IdTable relays{};
    IdTable inputs{};
    IdTable inputTriggers{};
    IdTable tempTriggers{};
};

/// `starterRelayId` = 0 — реле стартера не назначено.
Issue checkStep(ActionId action, const ChannelRef& ch, uint16_t starterRelayId, const ChannelTables& tables);

/// Короткое имя проблемы для журнала ("unknown action", ...); "" для None.
const char* issueToString(Issue issue);

} // namespace program_validate
