#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config/ConfigTypes.h"

/// Формат 8-байтного ROM 1-Wire: "AA:BB:CC:DD:EE:FF:GG:HH" (нужен `TextBytes::Sensors::ADDR_STRING`).
void formatSensorRomString(const uint8_t* addr8, char* out, size_t outSz);

/// Разбор "AA:BB:…" в 8 байт. false при пустой/битой строке (CRC здесь не проверяется).
bool parseSensorRomString(const char* rom, uint8_t out[8]);

/// Сверяет байты ROM с `cfg.sensors[].rom`; возвращает номер шёлка T1… или 0.
uint8_t resolveSensorConfigId(const uint8_t* addr8, const BaseConfig& cfg);
