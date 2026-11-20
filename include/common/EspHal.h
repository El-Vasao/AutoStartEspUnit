// include/common/EspHal.h
#pragma once

#include <Arduino.h>
#include <esp_system.h>
#if defined(ESP_PLATFORM)
#include <esp_task_wdt.h>
#endif

/**
 * Тонкие прослойки HAL ESP32-C3 (id чипа, куча, сторожевой таймер задачи).
 */
inline uint32_t espHalFreeHeap() {
    return ESP.getFreeHeap();
}

inline uint32_t espHalMaxBlock() {
    return ESP.getMaxAllocHeap();
}

inline void espHalFeedWdt() {
    yield();
#if defined(ESP_PLATFORM)
    (void)esp_task_wdt_reset();
#endif
}

inline void espHalWdtEnable() {
    // Arduino-ESP32 включает task WDT; кормление — espHalFeedWdt().
}

/** Младшие 24 бита MAC из eFuse — устойчивый суффикс MQTT client-id (вместо ESP.getChipId()). */
inline uint32_t espHalChipId() {
    const uint64_t mac = ESP.getEfuseMac();
    return static_cast<uint32_t>(mac & 0xFFFFFFull);
}

inline bool espHalChipIdValid() {
    return espHalChipId() != 0;
}
