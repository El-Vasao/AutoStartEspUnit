// include/common/Utils.h
#pragma once

#include <Arduino.h>
#include "common/EspHal.h"
#include <FS.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "common/Constants.h"

/** 
 * @brief Вспомогательные функции общего назначения (CRC, SSID, таймеры, преобразования).
 *
 * Примечание: здесь сознательно минимум динамики/глобальных объектов — утилиты часто вызываются из “горячих” путей.
 */

// CRC16 Modbus: один шаг и поток байт (полином 0xA001, старт 0xFFFF)
uint16_t crc16ModbusNext(uint16_t crc, uint8_t b);
uint16_t crc16ModbusFeedBytes(uint16_t crc, const uint8_t* data, size_t len);

/// CRC по открытому файлу; периодически espHalFeedWdt() на длинных чтениях.
uint16_t crc16ModbusStreamFile(File& f);

// Вычисление CRC16 (Modbus) для блока данных (эквивалент FeedBytes(0xFFFF, ...))
uint16_t calculateCRC16(const uint8_t* data, size_t len);

// Перегрузка для строковых данных
inline uint16_t calculateCRC16(const char* data, size_t len) {
    return calculateCRC16(reinterpret_cast<const uint8_t*>(data), len);
}

// Генерация уникального SSID на основе MAC-адреса (предпочтительно — без heap)
void generateUniqueSSID(char* dst, size_t dstLen, const char* prefix = APConfig::SSID_PREFIX);

// Проверка наличия кириллических символов в UTF-8 строке
bool containsCyrillic(const char* str);

/**
 * @brief Простой таймер для периодических операций.
 */
class SimpleTimer {
public:
    SimpleTimer(uint32_t interval);
    bool expired();                // проверить истечение интервала (автосброс)
    void reset();                  // принудительный сброс
    uint32_t getRemaining() const; // оставшееся время (мс)
private:
    uint32_t _last;
    uint32_t _interval;
};

// Удобная функция для периодических действий
inline bool every(uint32_t interval, uint32_t& last) {
    uint32_t now = millis();
    // Безопасно для переполнения millis(): вычитание unsigned корректно работает при wrap-around.
    if (now - last >= interval) {
        last = now;
        return true;
    }
    return false;
}

// Безопасное копирование строки (обёртка над strlcpy)
inline size_t safe_strcpy(char* dst, const char* src, size_t dstSize) {
    return strlcpy(dst, src, dstSize);
}

// Преобразование миллисекунд в секунды
inline uint32_t msToSec(uint32_t ms) { return ms / Time::MS_PER_SEC; }

// Преобразование секунд в миллисекунды
inline uint32_t secToMs(uint32_t sec) { return sec * Time::MS_PER_SEC; }

/// Ceil-divide milliseconds to whole seconds (timer remaining displays).
inline uint32_t msToSecCeil(uint32_t ms) {
    return ms ? (ms + Time::MS_PER_SEC - 1u) / Time::MS_PER_SEC : 0;
}

/// Pack calendar day as (tm_year & 0x1FF) << 9 | (tm_yday & 0x1FF) for once-per-day keys.
inline uint32_t calendarDayKey(const struct tm& t) {
    return (static_cast<uint32_t>(t.tm_year & 0x1FF) << 9) | static_cast<uint32_t>(t.tm_yday & 0x1FF);
}

// Проверка, находится ли значение в заданном диапазоне (включительно)
template<typename T>
inline bool inRange(T val, T min, T max) {
    return val >= min && val <= max;
}

// Макросы для атомарных блоков
#define ATOMIC_START() noInterrupts()
#define ATOMIC_END() interrupts()

