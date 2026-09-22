#pragma once

#include <stddef.h>

#include "mqtt/MqttCommand.h"

/// Parse flat cmd JSON (id, cmd, program, name, ref, enabled). No legacy action/*.
/// Returns true when syntactically valid and kind is known; check fields for args.
bool parseMqttCommandJson(const char* json, MqttCommand& out);

/// Best-effort extract `"id":"..."` from raw JSON (for parse-error replies).
bool mqttExtractReqId(const char* json, char* out, size_t outCap);
