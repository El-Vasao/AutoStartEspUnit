#pragma once

#include <stdint.h>

// Урезанный esp_partition_t: прошивке на хосте нужен label и идентичность указателя.
typedef struct {
    uint32_t address;
    uint32_t size;
    char label[17];
} esp_partition_t;
