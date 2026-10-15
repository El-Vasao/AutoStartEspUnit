// include/common/ErrorCodes.h
#pragma once

#include <Arduino.h>

/**
 * @brief Коды ошибок системы.
 *
 * Принцип:
 * - коды сгруппированы диапазонами (FS/GSM/ADC/MQTT/Program...), чтобы проще было расширять без конфликтов;
 * - числовые значения — внешний контракт (MQTT `last_err`, SSE): существующие не перенумеровывать;
 * - строковое представление короткое (< ErrorHistory::MSG_MAX, проверяет test_error_manager) и стабильное.
 */
enum class ErrorCode : uint8_t {
    NONE = 0,               ///< Ошибок нет
    FS_MOUNT_FAIL,          ///< Не удалось смонтировать файловую систему
    CONFIG_MISSING,         ///< Файл конфигурации отсутствует
    CONFIG_CRC_FAIL,        ///< Ошибка контрольной суммы конфигурации
    CONFIG_PARSE_FAIL,      ///< Ошибка парсинга JSON конфигурации
    FS_UNKNOWN,             ///< Неизвестная ошибка файловой системы
    WDT_RESET,              ///< Сброс по watchdog
    PANIC_RESET,            ///< Сброс после panic/exception
    BROWNOUT_RESET,         ///< Сброс по brownout
    UNEXPECTED_RESET,       ///< Прочий нештатный reset reason

    GSM_NO_RESPONSE = 16,   ///< GSM-модем не отвечает
    GSM_REG_FAIL,           ///< Не удалось зарегистрироваться в сети
    GSM_APN_FAIL,           ///< Ошибка подключения GPRS/APN

    ADC_READ_FAIL = 32,     ///< Ошибка чтения АЦП
    ONEWIRE_BUS_ERR,        ///< Ошибка шины 1-Wire

    MQTT_CONNECT_FAIL = 48, ///< Ошибка подключения к MQTT-брокеру
    RELAY_TIMEOUT = 64,     ///< Превышено время работы реле
    HW_MAP_INVALID = 72,    ///< Некорректная аппаратная карта (ID/пины)

    PROGRAM_ABORTED = 80,   ///< Программа прервана (аварийное завершение шага)
    PROGRAM_INVALID_REF,    ///< Программа отклонена или прервана: неверный id канала или неизвестный шаг
    START_BLOCKED,          ///< Запуск двигателя запрещён входом блокировки
    START_FAILED,           ///< Двигатель не завёлся за все попытки

    TIME_CCLK_FAIL = 88,    ///< Каскад времени: CCLK/NITZ не дал валидный clock
    TIME_CIPGSMLOC_FAIL,    ///< Каскад времени: CIPGSMLOC не дал валидный clock
    TIME_CNTP_FAIL,         ///< Каскад времени: CNTP/NTP не дал валидный clock

    UNKNOWN = 255           ///< Неизвестная ошибка
};

/**
 * @brief Преобразует код ошибки в строковое описание.
 * @param err Код ошибки.
 * @return Указатель на строку с описанием.
 */
inline const char* errorCodeToString(ErrorCode err) {
    switch(err) {
        case ErrorCode::NONE:              return "OK";
        case ErrorCode::FS_MOUNT_FAIL:     return "FS mount failed";
        case ErrorCode::CONFIG_MISSING:    return "Config missing";
        case ErrorCode::CONFIG_CRC_FAIL:   return "Config CRC mismatch";
        case ErrorCode::CONFIG_PARSE_FAIL: return "Config parse failed";
        case ErrorCode::FS_UNKNOWN:        return "Unknown FS error";
        case ErrorCode::WDT_RESET:         return "Watchdog reset";
        case ErrorCode::PANIC_RESET:       return "Panic reset";
        case ErrorCode::BROWNOUT_RESET:    return "Brownout reset";
        case ErrorCode::UNEXPECTED_RESET:  return "Unexpected reset";
        case ErrorCode::GSM_NO_RESPONSE:   return "GSM not responding";
        case ErrorCode::GSM_REG_FAIL:      return "GSM registration failed";
        case ErrorCode::GSM_APN_FAIL:      return "GSM APN failed";
        case ErrorCode::ADC_READ_FAIL:     return "ADC read failed";
        case ErrorCode::ONEWIRE_BUS_ERR:   return "1-Wire bus error";
        case ErrorCode::MQTT_CONNECT_FAIL: return "MQTT connection failed";
        case ErrorCode::RELAY_TIMEOUT:     return "Relay timeout";
        case ErrorCode::HW_MAP_INVALID:    return "Hardware map invalid";
        case ErrorCode::PROGRAM_ABORTED:   return "Program aborted";
        case ErrorCode::PROGRAM_INVALID_REF:return "Program has invalid step";
        case ErrorCode::START_BLOCKED:     return "Engine start blocked by interlock";
        case ErrorCode::START_FAILED:      return "Engine did not start";
        case ErrorCode::TIME_CCLK_FAIL:     return "Time CCLK failed";
        case ErrorCode::TIME_CIPGSMLOC_FAIL:return "Time CIPGSMLOC failed";
        case ErrorCode::TIME_CNTP_FAIL:     return "Time CNTP failed";
        default:                           return "Unknown error";
    }
}

