// src/config/internal/BaseConfigJsonIo.h — src/config/Config.BaseJsonEmit.cpp, Config.BaseSax.cpp
#pragma once

#include <Arduino.h>
#include <FS.h>
#include "config/ConfigTypes.h"

namespace config_internal {

/// Serialize `BaseConfig` to JSON; returns bytes written to `out` (excluding truncate short-writes).
size_t serializeBaseConfigToPrint(const BaseConfig& cfg, Print& out);

/// CRC16(Modbus) over the **same UTF-8** stream as serialize (for file integrity).
uint16_t crc16SerializedBaseConfig(const BaseConfig& cfg, size_t* outLen);

/// Как читать документ конфигурации.
enum class ParseMode : uint8_t {
    /// Принятие настроек из веба: обрезанный или синтаксически неверный JSON отклоняется целиком.
    Strict,
    /// Загрузка /config.json на старте: синтаксическая ошибка после корневого объекта не отменяет
    /// уже прочитанные поля (остальные — по умолчанию), чтобы повреждение не стирало весь конфиг.
    Lenient,
};

/// Populate `cfg` из JSON объекта конфига (SAX через JsonStreamingParser / JsonListener).
/// Корень не объект или ошибка полей (listener `failed`) — всегда false.
/// Optional: `outHasAdcCalibrate` / `outAdcCalibrateVoltage` capture ephemeral
/// `vehicle.adc_calibrate_voltage` (not stored in BaseConfig / not emitted).
bool parseBaseConfigStreamingFromFile(File& file, BaseConfig& cfg, ParseMode mode,
                                      bool* outHasAdcCalibrate = nullptr,
                                      float* outAdcCalibrateVoltage = nullptr);

/// Parse defaults from embedded PROGMEM JSON (full document object, strict).
bool parseBaseConfigStreamingFromProgmem(BaseConfig& cfg, const char* pgmDoc);

} // namespace config_internal
