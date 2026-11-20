#pragma once

// EspHal.h includes <esp_system.h>. Native builds have no ESP-IDF.
#include <stdint.h>

class EspClass {
public:
    uint32_t getFreeHeap() const { return 0; }
    uint32_t getMaxAllocHeap() const { return 0; }
    uint64_t getEfuseMac() const { return 0; }
};

inline EspClass ESP;
