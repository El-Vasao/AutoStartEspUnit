#include "common/Utils.h"
#include "common/EspHal.h"
#include <WiFi.h>
#include <FS.h>

/**
 * @file Utils.Core.cpp
 * @brief Реализация утилитарных функций (общая инфраструктура проекта).
 *
 * Ответственность:
 * - CRC16 helpers, генерация уникальных строк и небольшие pure-функции.
 *
 * Инварианты:
 * - Должно быть безопасно по памяти: без выделений heap и без больших временных буферов.
 * - Длинные операции (стрим CRC по файлу) должны кормить WDT и отдавать управление SDK (yield).
 *
 * Запрещено:
 * - Добавлять сюда логику подсистем (web/gsm/mqtt и т.д.). Это только общие утилиты.
 * - Использовать `String` в горячих путях.
 */

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

uint16_t crc16ModbusStreamFile(File& f) {
    uint16_t crc = 0xFFFF;
    uint8_t buf[128];
    uint32_t fed = 0;
    while (f.available()) {
        const int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        crc = crc16ModbusFeedBytes(crc, buf, static_cast<size_t>(n));
        fed += static_cast<unsigned>(n);
        if ((fed & 0xFFu) == 0) {
            espHalFeedWdt();
            // Важно: для ESP8266 одного wdtFeed недостаточно — нужно иногда отдавать управление SDK.
            yield();
        }
    }
    return crc;
}

uint16_t calculateCRC16(const uint8_t* data, size_t len) {
    return crc16ModbusFeedBytes(0xFFFF, data, len);
}

void generateUniqueSSID(char* dst, size_t dstLen, const char* prefix) {
    if (!dst || dstLen == 0) return;
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(dst, dstLen, "%s%02X%02X%02X", prefix, mac[3], mac[4], mac[5]);
    dst[dstLen - 1] = '\0';
}

bool containsCyrillic(const char* str) {
    if (!str) return false;
    while (*str) {
        unsigned char c = *str;
        // UTF-8 Cyrillic bytes typically start with 0xD0/0xD1
        if (c == 0xD0 || c == 0xD1) return true;
        str++;
    }
    return false;
}

