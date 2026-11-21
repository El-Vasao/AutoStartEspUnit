// include/modem/AtParam.h
#pragma once

#include <stdint.h>

/**
 * Строки для AT в кавычках / ATD / тела SMS: без инъекции команд.
 * Чистые проверки, без Arduino (native-тесты).
 */
namespace at_param {

/// APN, USER, PWD, broker, NTP: нельзя `"`, CR, LF.
inline bool quotedOk(const char* s) {
    if (!s) return true;
    for (; *s; ++s) {
        if (*s == '"' || *s == '\r' || *s == '\n') return false;
    }
    return true;
}

/// owner_phone для ATD / CMGS: цифры и + * #.
inline bool phoneOk(const char* s) {
    if (!s || !*s) return false;
    for (; *s; ++s) {
        const char c = *s;
        if ((c >= '0' && c <= '9') || c == '+' || c == '*' || c == '#') continue;
        return false;
    }
    return true;
}

/// Тело SMS до Ctrl+Z: нельзя 0x1A / 0x1B.
inline bool smsBodyOk(const char* s) {
    if (!s || !*s) return false;
    for (; *s; ++s) {
        const auto b = static_cast<uint8_t>(*s);
        if (b == 0x1A || b == 0x1B) return false;
    }
    return true;
}

} // namespace at_param
