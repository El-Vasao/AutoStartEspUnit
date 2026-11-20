#pragma once

// EspHal.h подключает <esp_system.h>. У нативной сборки нет ESP-IDF.
#include <stdint.h>

class EspClass {
public:
    uint32_t getFreeHeap() const { return 0; }
    uint32_t getMaxAllocHeap() const { return 0; }
    uint64_t getEfuseMac() const { return 0; }
};

inline EspClass ESP;
