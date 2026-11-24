#pragma once

#include <stddef.h>

#include "mqtt/MqttCommand.h"

bool parseMqttCommandJson(const char* json, MqttCommand& out);
bool mqttExtractReqId(const char* json, char* out, size_t outCap);

/// Извлечь строковое поле `"data":"..."` (base64) из JSON команды put. outCap включает '\0'.
bool mqttExtractDebugDataB64(const char* json, char* out, size_t outCap);
