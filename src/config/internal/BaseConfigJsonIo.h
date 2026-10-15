// include/config/internal/BaseConfigJsonIo.h — src/config/Config.BaseJsonEmit.cpp, Config.BaseSax.cpp
#pragma once

#include <Arduino.h>
#include <FS.h>
#include "config/ConfigTypes.h"

namespace config_internal {

/// Serialize `BaseConfig` to JSON; returns bytes written to `out` (excluding truncate short-writes).
size_t serializeBaseConfigToPrint(const BaseConfig& cfg, Print& out);

/// CRC16(Modbus) over the **same UTF-8** stream as serialize (for file integrity).
uint16_t crc16SerializedBaseConfig(const BaseConfig& cfg, size_t* outLen);

/// Populate `cfg` из JSON объекта конфига (SAX через JsonStreamingParser / JsonListener; семантика полей как прежний fill).
/// Listener sets `failed`/`rootNotObject` on error; clears before parse starts.
/// Optional: `outHasAdcCalibrate` / `outAdcCalibrateVoltage` capture ephemeral
/// `vehicle.adc_calibrate_voltage` (not stored in BaseConfig / not emitted).
bool parseBaseConfigStreamingFromFile(File& file, BaseConfig& cfg,
                                      bool* outHasAdcCalibrate = nullptr,
                                      float* outAdcCalibrateVoltage = nullptr);

/// Parse defaults from embedded PROGMEM JSON (full document object).
bool parseBaseConfigStreamingFromProgmem(BaseConfig& cfg, const char* pgmDoc);

} // namespace config_internal
