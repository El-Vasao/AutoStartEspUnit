// src/config/internal/JsonScalar.h
#pragma once

#include <stdint.h>

/**
 * Разбор скалярных значений из SAX-парсера JSON (значение приходит текстом).
 * Общий для config.json и файлов программ. Любое значение, которое нельзя целиком разобрать
 * как число нужного вида, даёт `def`. Целые больше предела типа насыщаются до предела.
 */
namespace json_scalar {

/// "true"/"1" → true, "false"/"0" → false, иначе def.
bool parseBool(const char* v, bool def);
/// Десятичное без знака, без пробелов и знака минус; переполнение uint32 → def.
uint32_t parseU32(const char* v, uint32_t def);
uint16_t parseU16(const char* v, uint16_t def);
uint8_t parseU8(const char* v, uint8_t def);
/// Десятичное со знаком, насыщение до [-32768, 32767].
int16_t parseI16(const char* v, int16_t def);
/// Конечное число с плавающей точкой; NaN/inf и мусор → def.
float parseF(const char* v, float def);

} // namespace json_scalar
