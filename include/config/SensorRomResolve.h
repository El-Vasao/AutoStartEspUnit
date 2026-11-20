#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config/ConfigTypes.h"

/// Формат 8-байтного ROM 1-Wire: "AA:BB:CC:DD:EE:FF:GG:HH" (нужен `TextBytes::Sensors::ADDR_STRING`).
void formatSensorRomString(const uint8_t* addr8, char* out, size_t outSz);

/// Сверяет байты ROM с `cfg.sensors[].rom`; возвращает номер шёлка T1… или 0.
uint8_t resolveSensorConfigId(const uint8_t* addr8, const BaseConfig& cfg);
