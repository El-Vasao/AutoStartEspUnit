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

/// Заполняет `cfg` из JSON объекта конфига (SAX через JsonStreamingParser / JsonListener).
/// Корень не объект или ошибка полей (listener `failed`) — всегда false.
/// Необязательно: `outHasAdcCalibrate` / `outAdcCalibrateVoltage` забирают эфемерное
/// `vehicle.adc_calibrate_voltage` (в BaseConfig не хранится и наружу не пишется).
bool parseBaseConfigStreamingFromFile(File& file, BaseConfig& cfg, ParseMode mode,
                                      bool* outHasAdcCalibrate = nullptr,
                                      float* outAdcCalibrateVoltage = nullptr);

/// Разобрать значения по умолчанию из встроенного JSON в PROGMEM (целый объект документа, строго).
bool parseBaseConfigStreamingFromProgmem(BaseConfig& cfg, const char* pgmDoc);

} // namespace config_internal
