#pragma once

#include <stddef.h>
#include <stdint.h>

#include <Arduino.h>

/// FNV-1a 32 (offset 2166136261, prime 16777619). Opaque catalog token, not a checksum of files.
constexpr uint32_t kFnv1a32Offset = 2166136261u;

uint32_t fnv1a32Feed(uint32_t hash, uint8_t b);
uint32_t fnv1a32(const uint8_t* data, size_t len);

inline uint32_t fnv1a32(const char* data, size_t len) {
    return fnv1a32(reinterpret_cast<const uint8_t*>(data), len);
}

/// Print that only folds written bytes into FNV-1a 32.
class Fnv1aPrint : public Print {
public:
    size_t write(uint8_t c) override {
        hash_ = fnv1a32Feed(hash_, c);
        bytes_++;
        return 1;
    }
    uint32_t hash() const { return hash_; }
    size_t bytes() const { return bytes_; }

private:
    uint32_t hash_{kFnv1a32Offset};
    size_t bytes_{0};
};

inline void writeFnv1a32Hex(Print& p, uint32_t h) {
    char buf[8];
    for (int i = 0; i < 8; i++) {
        const unsigned nibble = (h >> ((7 - i) * 4)) & 0xFu;
        buf[i] = (char)(nibble < 10 ? ('0' + nibble) : ('a' + (nibble - 10)));
    }
    p.write(buf, 8);
}
