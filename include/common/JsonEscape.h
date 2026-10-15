// include/common/JsonEscape.h
#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Экранирование строк для JSON — единственная реализация для конфигурации, программ, SSE и MQTT.
 * `"` и `\` экранируются, \b \f \n \r \t — короткими формами, прочие байты < 0x20 — как \u00XX.
 * Байты UTF-8 (>= 0x80) проходят без изменений.
 */
namespace json_escape {

/// Записывает экранированную форму одного байта в `out` (до 6 символов); возвращает длину.
uint8_t escapeByte(uint8_t c, char out[6]);

/// Экранирует `s` в `out` (с завершающим нулём, без кавычек). Возвращает длину, нужную полностью;
/// при нехватке места строка обрезается по границе экранированного символа.
size_t escapeTo(char* out, size_t cap, const char* s);

/// Тело JSON-строки без кавычек. nullptr — ничего не пишет.
size_t writeEscaped(Print& p, const char* s);

/// JSON-строка в кавычках; nullptr — `""`.
size_t writeQuoted(Print& p, const char* s);

} // namespace json_escape
