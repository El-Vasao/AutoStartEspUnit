#include "common/Fnv1a.h"

uint32_t fnv1a32Feed(uint32_t hash, uint8_t b) {
    hash ^= b;
    hash *= 16777619u;
    return hash;
}

uint32_t fnv1a32(const uint8_t* data, size_t len) {
    uint32_t h = kFnv1a32Offset;
    if (!data) return h;
    for (size_t i = 0; i < len; i++) {
        h = fnv1a32Feed(h, data[i]);
    }
    return h;
}
