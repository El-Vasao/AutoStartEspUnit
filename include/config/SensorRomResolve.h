#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config/ConfigTypes.h"

/// Format 8-byte 1-Wire ROM as "AA:BB:CC:DD:EE:FF:GG:HH" (needs `TextBytes::Sensors::ADDR_STRING`).
void formatSensorRomString(const uint8_t* addr8, char* out, size_t outSz);

/// Match ROM bytes against `cfg.sensors[].rom`; returns config id or 0 if unmapped / null addr.
uint16_t resolveSensorConfigId(const uint8_t* addr8, const BaseConfig& cfg);
