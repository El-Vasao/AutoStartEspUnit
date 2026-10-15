// src/common/Utils.cpp
#include "common/Utils.h"

#include <WiFi.h>

uint16_t crc16ModbusStreamFile(File& f) {
    uint16_t crc = 0xFFFF;
    uint8_t buf[128];
    uint32_t fed = 0;
    while (f.available()) {
        const int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        crc = crc16ModbusFeedBytes(crc, buf, static_cast<size_t>(n));
        fed += static_cast<unsigned>(n);
        if ((fed & 0xFFu) == 0) espHalFeedWdt();
    }
    return crc;
}

void generateUniqueSSID(char* dst, size_t dstLen, const char* prefix) {
    if (!dst || dstLen == 0) return;
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(dst, dstLen, "%s%02X%02X%02X", prefix, mac[3], mac[4], mac[5]);
    dst[dstLen - 1] = '\0';
}
