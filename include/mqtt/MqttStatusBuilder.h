#pragma once

#include <Arduino.h>

#include "app/StatusSnapshot.h"
#include "config/ConfigTypes.h"

/// Canonical nested MQTT JSON status (see docs/modules/mqtt.md § StatusSnapshot).
/// Full: `"full":true`, `uptime`, `mode`, `time`, `hw`, `radio`, `program`, `runtime`, `diag`
/// (`diag.last_err` omitted when empty). Compact wire (no pretty-print).
void emitMqttStatusJson(const StatusSnapshot& s, const BaseConfig& cfg, Print& p);

/// Partial status for merge consumers: `"full":false`, always `uptime`, then changed objects/fields.
void emitMqttStatusDeltaJson(const StatusSnapshot& cur, const StatusSnapshot& prev, const BaseConfig& cfg,
                             Print& p);

/// True when meaningful fields differ (ignores `uptime`, `time.epoch` tick, temp `lastMs`;
/// float/heap/timer ε from `JsonBytes::Mqtt`).
bool mqttStatusHasSignificantChanges(const StatusSnapshot& cur, const StatusSnapshot& prev,
                                     const BaseConfig& cfg);
