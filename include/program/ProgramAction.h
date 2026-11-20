#pragma once

#include <stdint.h>

/// Скомпилированный id действия для исполнения программы (без строк на горячем пути).
enum class ActionId : uint8_t {
    UNKNOWN = 0,

    // С таймером / с состоянием
    WAIT,
    RELAY_PULSE_ON_OFF_ON,
    RELAY_PULSE_OFF_ON_OFF,
    /// Запуск двигателя: алгоритм, вход, тайминги и попытки берутся из config.vehicle.
    ENGINE_START,

    // Мгновенные
    RELAY_ON,
    RELAY_OFF,
    RELAY_TOGGLE,
    INPUT_ENABLE,
    INPUT_DISABLE,
    INPUT_TRIGGER_ENABLE,
    INPUT_TRIGGER_DISABLE,
    TEMP_TRIGGER_ENABLE,
    TEMP_TRIGGER_DISABLE,
    BATTERY_SAVER_ON,
    BATTERY_SAVER_OFF,
    THERMOSTAT_ON,
    THERMOSTAT_OFF,

    /// Звонок на config.gsm.owner_phone (голос); ждать `ms`, затем положить трубку.
    CALL_OWNER,
    /// SMS на owner_phone с текстом шага `message`.
    SMS_OWNER,
};

/// Единственный источник соответствия ActionId ↔ строка JSON/программы.
ActionId actionIdFromString(const char* action);
/// Каноническое имя действия или "" для UNKNOWN / неверного id.
const char* actionIdToString(ActionId id);

/// Шаг крутит реле стартера (защиты, экономия АКБ, правила прерывания).
bool isStarterAction(ActionId id);

/// Шаг адресует реле через `relay_id` (RELAY_ON/OFF/TOGGLE/PULSE_*).
bool isRelayAction(ActionId id);

