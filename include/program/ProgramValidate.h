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
};

/// `starterRelayId` = 0 — реле стартера не назначено.
Issue checkStep(ActionId action, uint16_t relayId, uint16_t starterRelayId);

/// Короткое имя проблемы для журнала ("unknown action", ...); "" для None.
const char* issueToString(Issue issue);

} // namespace program_validate
