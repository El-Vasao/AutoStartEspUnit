#pragma once

#include <Arduino.h>

#include "app/StatusSnapshot.h"
#include "config/ConfigTypes.h"

/// Canonical MQTT JSON status (see docs/modules/mqtt.md § StatusSnapshot).
/// Order: uptime, mode, voltage, engineRunning, inputsById, relaysById, tempSensorsById,
/// current_program?, last_program, runtime, last_error. No schema field; fields are not truncated.
void emitMqttStatusJson(const StatusSnapshot& s, const BaseConfig& cfg, Print& p);
