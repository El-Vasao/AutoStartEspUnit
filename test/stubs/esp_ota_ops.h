#pragma once

// Два слота OTA. Update.end переключает boot на следующий; pin/activate двигают его обратно.
#include "esp_partition.h"

#include <string.h>

typedef int esp_err_t;
#define ESP_OK 0

struct EspOtaHostState {
    esp_partition_t running{};
    esp_partition_t next{};
    const esp_partition_t* boot{nullptr};

    EspOtaHostState() {
        running.address = 0x10000;
        running.size = 0x140000;
        memcpy(running.label, "app0", 5);
        next.address = 0x150000;
        next.size = 0x140000;
        memcpy(next.label, "app1", 5);
        boot = &running;
    }
};

inline EspOtaHostState& espOtaHostState() {
    static EspOtaHostState state;
    return state;
}

inline const esp_partition_t* esp_ota_get_running_partition(void) {
    return &espOtaHostState().running;
}

inline const esp_partition_t* esp_ota_get_boot_partition(void) {
    return espOtaHostState().boot;
}

inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t* partition) {
    if (!partition) return -1;
    espOtaHostState().boot = partition;
    return ESP_OK;
}

// Слот, который Update.end делает загрузочным (не выполняемый образ).
inline const esp_partition_t* esp_ota_test_next_partition(void) {
    return &espOtaHostState().next;
}

inline void esp_ota_test_switch_boot_to_next(void) {
    espOtaHostState().boot = &espOtaHostState().next;
}

inline void esp_ota_test_reset(void) {
    espOtaHostState().boot = &espOtaHostState().running;
}
