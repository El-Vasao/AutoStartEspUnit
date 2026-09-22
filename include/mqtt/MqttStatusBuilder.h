#pragma once

#include <Arduino.h>

#include "app/StatusSnapshot.h"
#include "config/ConfigTypes.h"

/// Canonical MQTT JSON status (see docs/modules/mqtt.md § StatusSnapshot).
/// Full: leading `"full":true`, then uptime, mode, voltage, engineRunning, inputsById,
/// relaysById, tempSensorsById, current_program?, last_program, runtime, last_error.
/// No schema field; fields are not truncated.
void emitMqttStatusJson(const StatusSnapshot& s, const BaseConfig& cfg, Print& p);

/// Partial status for merge consumers: leading `"full":false`, then only changed fields.
/// Always includes `uptime` when at least one significant change exists (caller must check).
void emitMqttStatusDeltaJson(const StatusSnapshot& cur, const StatusSnapshot& prev, const BaseConfig& cfg,
                             Print& p);

/// True when meaningful fields differ (ignores `uptime` and temp `lastMs`; float ε from Constants).
bool mqttStatusHasSignificantChanges(const StatusSnapshot& cur, const StatusSnapshot& prev,
                                     const BaseConfig& cfg);
