#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "config/ConfigTypes.h"

/// Тело объекта `hwMap`: relays, inputs, sensors, scheduleTriggers.
void emitHwMapObject(const BaseConfig& cfg, Print& p);

/// Тело объекта `trigMap`: inputTriggers, tempTriggers.
void emitTrigMapObject(const BaseConfig& cfg, Print& p);

/// FNV-1a 32 от байтов `emitHwMapObject` (retained-хеш на `hwMap`).
uint32_t hwMapObjectFnv(const BaseConfig& cfg);

/// FNV-1a 32 от байтов `emitTrigMapObject` (retained-хеш на `trigMap`).
uint32_t trigMapObjectFnv(const BaseConfig& cfg);
