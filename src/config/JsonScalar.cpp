// src/config/JsonScalar.cpp
#include "config/internal/JsonScalar.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace json_scalar {

namespace {
/// Whole-string decimal without sign or whitespace that fits uint32.
bool readUnsigned(const char* v, uint32_t& out) {
    if (!v || v[0] < '0' || v[0] > '9') return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long long n = strtoull(v, &end, 10);
    if (errno == ERANGE || !end || *end != '\0' || n > 0xFFFFFFFFULL) return false;
    out = static_cast<uint32_t>(n);
    return true;
}
} // namespace

bool parseBool(const char* v, bool def) {
    if (!v || !*v) return def;
    if (strcmp(v, "true") == 0 || strcmp(v, "1") == 0) return true;
    if (strcmp(v, "false") == 0 || strcmp(v, "0") == 0) return false;
    return def;
}

uint32_t parseU32(const char* v, uint32_t def) {
    uint32_t n = 0;
    return readUnsigned(v, n) ? n : def;
}

uint16_t parseU16(const char* v, uint16_t def) {
    uint32_t n = 0;
    if (!readUnsigned(v, n)) return def;
    return n > 65535u ? 65535 : static_cast<uint16_t>(n);
}

uint8_t parseU8(const char* v, uint8_t def) {
    uint32_t n = 0;
    if (!readUnsigned(v, n)) return def;
    return n > 255u ? 255 : static_cast<uint8_t>(n);
}

int16_t parseI16(const char* v, int16_t def) {
    if (!v || !*v) return def;
    const char* digits = (*v == '-') ? v + 1 : v;
    if (*digits < '0' || *digits > '9') return def;
    errno = 0;
    char* end = nullptr;
    const long long n = strtoll(v, &end, 10);
    if (!end || *end != '\0') return def;
    if (errno == ERANGE) return (*v == '-') ? -32768 : 32767;
    if (n > 32767) return 32767;
    if (n < -32768) return -32768;
    return static_cast<int16_t>(n);
}

float parseF(const char* v, float def) {
    if (!v || !*v) return def;
    char* end = nullptr;
    const float f = strtof(v, &end);
    if (!end || end == v || *end != '\0' || !isfinite(f)) return def;
    return f;
}

} // namespace json_scalar
