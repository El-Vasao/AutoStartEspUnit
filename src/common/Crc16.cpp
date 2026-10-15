// src/common/Crc16.cpp
#include "common/Crc16.h"

uint16_t crc16ModbusNext(uint16_t crc, uint8_t b) {
    crc ^= b;
    for (int j = 0; j < 8; j++) {
        if (crc & 1)
            crc = (crc >> 1) ^ 0xA001;
        else
            crc >>= 1;
    }
    return crc;
}

uint16_t crc16ModbusFeedBytes(uint16_t crc, const uint8_t* data, size_t len) {
    if (!data) return crc;
    for (size_t i = 0; i < len; i++) crc = crc16ModbusNext(crc, data[i]);
    return crc;
}

uint16_t calculateCRC16(const uint8_t* data, size_t len) {
    return crc16ModbusFeedBytes(0xFFFF, data, len);
}
