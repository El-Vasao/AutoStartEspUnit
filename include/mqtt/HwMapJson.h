#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "config/ConfigTypes.h"

/// Object `hw` inside the `hwmap` reply. Same bytes are hashed into `rev.hw`.
void emitHwMapObject(const BaseConfig& cfg, Print& p);

/// FNV-1a 32 of `emitHwMapObject` bytes.
uint32_t hwMapObjectFnv(const BaseConfig& cfg);
