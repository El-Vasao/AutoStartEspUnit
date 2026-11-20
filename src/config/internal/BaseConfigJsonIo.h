// src/config/internal/BaseConfigJsonIo.h — src/config/Config.BaseJsonEmit.cpp, Config.BaseSax.cpp
#pragma once

#include <Arduino.h>
#include <FS.h>
#include "config/ConfigTypes.h"

namespace config_internal {

/// Сериализует `BaseConfig` в JSON; возвращает число байт, записанных в `out` (усечённые короткие записи не считаются).
size_t serializeBaseConfigToPrint(const BaseConfig& cfg, Print& out);

/// CRC16(Modbus) по **тому же потоку UTF-8**, что и сериализация (для целостности файла).
uint16_t crc16SerializedBaseConfig(const BaseConfig& cfg, size_t* outLen);

/// Как читать документ конфигурации.
enum class ParseMode : uint8_t {
    /// Принятие настроек из веба: обрезанный или синтаксически неверный JSON отклоняется целиком.
    Strict,
    /// Загрузка /config.json на старте: синтаксическая ошибка после корневого объекта не отменяет
    /// уже прочитанные поля (остальные — по умолчанию), чтобы повреждение не стирало весь конфиг.
    Lenient,
};

/// Биты секций, увиденных при SAX (для Strict POST).
namespace ConfigSection {
    constexpr uint16_t Wifi = 1u << 0;
    constexpr uint16_t Gsm = 1u << 1;
    constexpr uint16_t Mqtt = 1u << 2;
    constexpr uint16_t Vehicle = 1u << 3;
    constexpr uint16_t Inputs = 1u << 4;
    constexpr uint16_t Relays = 1u << 5;
    constexpr uint16_t InputTriggers = 1u << 6;
    constexpr uint16_t TempTriggers = 1u << 7;
    constexpr uint16_t RequiredPost =
        Wifi | Gsm | Mqtt | Vehicle | Inputs | Relays | InputTriggers | TempTriggers;
}

/// Заполняет `cfg` из JSON объекта конфига (SAX через JsonStreamingParser / JsonListener).
/// Корень не объект или ошибка полей (listener `failed`) — всегда false.
/// Необязательно: `outHasAdcCalibrate` / `outAdcCalibrateVoltage` забирают эфемерное
/// `vehicle.adc_calibrate_voltage` (в BaseConfig не хранится и наружу не пишется).
/// `outSeenSections` — битовая маска ConfigSection::* (если не nullptr).
bool parseBaseConfigStreamingFromFile(File& file, BaseConfig& cfg, ParseMode mode,
                                      bool* outHasAdcCalibrate = nullptr,
                                      float* outAdcCalibrateVoltage = nullptr,
                                      uint16_t* outSeenSections = nullptr);

/// Разобрать значения по умолчанию из встроенного JSON в PROGMEM (целый объект документа, строго).
bool parseBaseConfigStreamingFromProgmem(BaseConfig& cfg, const char* pgmDoc);

} // namespace config_internal
