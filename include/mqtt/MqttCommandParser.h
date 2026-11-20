#pragma once

#include <stddef.h>

#include "mqtt/MqttCommand.h"

/// Разбор JSON cmd (id, cmd, program, name, ref, enabled).
/// true, если синтаксис верен и вид известен; аргументы смотреть в полях.
bool parseMqttCommandJson(const char* json, MqttCommand& out);

/// По возможности вытащить `"id":"..."` из сырого JSON (для ответов об ошибке разбора).
bool mqttExtractReqId(const char* json, char* out, size_t outCap);
