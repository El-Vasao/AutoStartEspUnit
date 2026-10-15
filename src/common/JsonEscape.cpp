// src/common/JsonEscape.cpp
#include "common/JsonEscape.h"

#include <string.h>

namespace json_escape {

uint8_t escapeByte(uint8_t c, char out[6]) {
    switch (c) {
        case '"':  out[0] = '\\'; out[1] = '"';  return 2;
        case '\\': out[0] = '\\'; out[1] = '\\'; return 2;
        case '\b': out[0] = '\\'; out[1] = 'b';  return 2;
        case '\f': out[0] = '\\'; out[1] = 'f';  return 2;
        case '\n': out[0] = '\\'; out[1] = 'n';  return 2;
        case '\r': out[0] = '\\'; out[1] = 'r';  return 2;
        case '\t': out[0] = '\\'; out[1] = 't';  return 2;
        default: break;
    }
    if (c < 0x20u) {
        static const char kHex[] = "0123456789abcdef";
        out[0] = '\\';
        out[1] = 'u';
        out[2] = '0';
        out[3] = '0';
        out[4] = kHex[(c >> 4) & 0x0F];
        out[5] = kHex[c & 0x0F];
        return 6;
    }
    out[0] = (char)c;
    return 1;
}

size_t escapeTo(char* out, size_t cap, const char* s) {
    size_t need = 0;
    size_t w = 0;
    bool full = (cap == 0);
    if (s) {
        for (const unsigned char* u = reinterpret_cast<const unsigned char*>(s); *u; u++) {
            char e[6];
            const uint8_t n = escapeByte(*u, e);
            need += n;
            if (!full && w + n < cap) {
                memcpy(out + w, e, n);
                w += n;
            } else {
                full = true;
            }
        }
    }
    if (cap) out[w] = '\0';
    return need;
}

size_t writeEscaped(Print& p, const char* s) {
    if (!s) return 0;
    size_t n = 0;
    for (const unsigned char* u = reinterpret_cast<const unsigned char*>(s); *u; u++) {
        char e[6];
        const uint8_t len = escapeByte(*u, e);
        n += p.write(reinterpret_cast<const uint8_t*>(e), len);
    }
    return n;
}

size_t writeQuoted(Print& p, const char* s) {
    size_t n = p.write((uint8_t)'"');
    n += writeEscaped(p, s);
    n += p.write((uint8_t)'"');
    return n;
}

} // namespace json_escape
