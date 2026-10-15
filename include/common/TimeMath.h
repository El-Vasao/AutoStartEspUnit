// include/common/TimeMath.h
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <time.h>

#include "common/Constants.h"

/**
 * Арифметика времени на `millis()`. Интервалы считаются разностью беззнаковых значений, поэтому
 * переполнение `millis()` (каждые ~49,7 суток) не ломает сравнения. Сравнивать моменты напрямую
 * (`now > deadline`) нельзя — только `(int32_t)(now - deadline) >= 0` или `now - last >= interval`.
 */

/// true раз в `interval` мс; `last` хранит момент последнего срабатывания.
inline bool every(uint32_t interval, uint32_t& last) {
    const uint32_t now = millis();
    if (now - last >= interval) {
        last = now;
        return true;
    }
    return false;
}

inline uint32_t secToMs(uint32_t sec) { return sec * Time::MS_PER_SEC; }

/// Миллисекунды в секунды с округлением вверх (оставшееся время в UI).
inline uint32_t msToSecCeil(uint32_t ms) {
    return ms ? (ms + Time::MS_PER_SEC - 1u) / Time::MS_PER_SEC : 0;
}

/// Ключ календарного дня `(tm_year & 0x1FF) << 9 | tm_yday` для правил «раз в день».
inline uint32_t calendarDayKey(const struct tm& t) {
    return (static_cast<uint32_t>(t.tm_year & 0x1FF) << 9) | static_cast<uint32_t>(t.tm_yday & 0x1FF);
}
