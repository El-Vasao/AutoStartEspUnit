// include/io/HwMap.h
#pragma once

#include <Arduino.h>
#include "common/Pins.h"
#include "common/Constants.h"

/**
 * @brief Единое соответствие id каналов железа и внутренних индексов.
 *
 * Контракт с приоритетом безопасности:
 * - Внешние и прикладные слои должны пользоваться id.
 * - Драйверы могут хранить состояние по индексу, но не должны принимать индексы снаружи.
 */
namespace HwMap {

inline int8_t relayIndexById(uint16_t relayId) {
    if (!relayId) return -1;
    for (uint8_t i = 0; i < HardwareLimits::RELAYS; i++) {
        if (Pin::RELAY_IDS[i] == relayId) return (int8_t)i;
    }
    return -1;
}

inline int8_t inputIndexById(uint16_t inputId) {
    if (!inputId) return -1;
    for (uint8_t i = 0; i < HardwareLimits::INPUTS; i++) {
        if (Pin::INPUT_IDS[i] == inputId) return (int8_t)i;
    }
    return -1;
}

/// Проверка таблицы Pin::*_IDS при старте: пустая таблица, нулевой id или повтор id — неверно.
inline bool idsHaveZeroOrDuplicates(const uint16_t* ids, size_t n) {
    if (!ids || n == 0) return true;
    for (size_t i = 0; i < n; i++) {
        if (ids[i] == 0) return true;
        for (size_t j = i + 1; j < n; j++) {
            if (ids[i] == ids[j]) return true;
        }
    }
    return false;
}

} // namespace HwMap

