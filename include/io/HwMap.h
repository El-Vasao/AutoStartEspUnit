// include/io/HwMap.h
#pragma once

#include <Arduino.h>
#include "common/Constants.h"
#include "common/ChannelSilk.h"

/**
 * Соответствие публичного номера канала (1…n, 0 = нет) и индекса слота 0…n-1.
 */
namespace HwMap {

inline int8_t relayIndexByNumber(uint8_t number) {
    return ChannelSilk::slotFromNumber(number, ChannelSilk::Kind::Relay);
}

inline int8_t inputIndexByNumber(uint8_t number) {
    return ChannelSilk::slotFromNumber(number, ChannelSilk::Kind::Input);
}

inline int8_t sensorIndexByNumber(uint8_t number) {
    return ChannelSilk::slotFromNumber(number, ChannelSilk::Kind::Sensor);
}

/// Совместимость со старыми вызовами: number — 1…n (шёлк), не 2001.
inline int8_t relayIndexById(uint16_t relayId) {
    if (relayId == 0 || relayId > 255) return -1;
    return relayIndexByNumber(static_cast<uint8_t>(relayId));
}

inline int8_t inputIndexById(uint16_t inputId) {
    if (inputId == 0 || inputId > 255) return -1;
    return inputIndexByNumber(static_cast<uint8_t>(inputId));
}

} // namespace HwMap
