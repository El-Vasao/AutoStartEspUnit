#pragma once

#include <Arduino.h>

#include "app/StatusSnapshot.h"
#include "config/ConfigTypes.h"

/// Канонический вложенный JSON статуса MQTT (см. docs/modules/mqtt.md).
/// Full: `"full":true`, `sid`, `seq`, `uptime`, `mode`, `time`, `io`, `csq`, `program`,
/// `runtime`, `err` (опустить, если пусто), `rev`. Компактно на линии (без pretty-print).
void emitMqttStatusJson(const StatusSnapshot& s, const BaseConfig& cfg, Print& p);

/// Частичный статус для слияния у получателя: `"full":false`, всегда `sid`/`seq`/`uptime`, затем изменившиеся ключи.
void emitMqttStatusDeltaJson(const StatusSnapshot& cur, const StatusSnapshot& prev, const BaseConfig& cfg,
                             Print& p);

/// Истина, когда значимые поля различаются (тик `uptime` и `time.epoch` не считается;
/// ε float/таймера из `JsonBytes::Mqtt`). Heap на линии MQTT нет.
bool mqttStatusHasSignificantChanges(const StatusSnapshot& cur, const StatusSnapshot& prev,
                                     const BaseConfig& cfg);

/// Дискретные изменения: дельту публиковать, не дожидаясь `publish_interval_sec`.
/// Само по себе не считается ни напряжение, ни температура, ни CSQ, ни heap, ни один лишь `timerRemaining`.
bool mqttStatusHasEdgeChanges(const StatusSnapshot& cur, const StatusSnapshot& prev,
                              const BaseConfig& cfg);
