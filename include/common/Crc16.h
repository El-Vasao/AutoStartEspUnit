// include/common/Crc16.h
#pragma once

#include <stddef.h>
#include <stdint.h>

/// CRC16 Modbus (полином 0xA001, старт 0xFFFF): шаг, поток байт и целый блок. Без Arduino.
uint16_t crc16ModbusNext(uint16_t crc, uint8_t b);
uint16_t crc16ModbusFeedBytes(uint16_t crc, const uint8_t* data, size_t len);

/// CRC блока целиком (эквивалент crc16ModbusFeedBytes(0xFFFF, ...)).
uint16_t calculateCRC16(const uint8_t* data, size_t len);

inline uint16_t calculateCRC16(const char* data, size_t len) {
    return calculateCRC16(reinterpret_cast<const uint8_t*>(data), len);
}
