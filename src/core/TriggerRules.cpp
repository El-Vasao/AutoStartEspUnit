// src/core/TriggerRules.cpp
#include "core/TriggerRules.h"

#include <string.h>

namespace trigger_rules {

Compare compareFromString(const char* s) {
    if (!s) return Compare::Invalid;
    if (strcmp(s, "above") == 0) return Compare::Above;
    if (strcmp(s, "below") == 0) return Compare::Below;
    return Compare::Invalid;
}

bool EdgeLatch::update(bool condition) {
    if (!condition) {
        latched = false;
        return false;
    }
    if (latched) return false;
    latched = true;
    return true;
}

bool ThresholdLatch::update(float value, float threshold, Compare cmp, float hysteresis) {
    if (cmp == Compare::Invalid) {
        latched = false;
        return false;
    }
    if (hysteresis < 0.0f) hysteresis = 0.0f;
    if (latched) {
        const bool released = (cmp == Compare::Above) ? (value <= threshold - hysteresis)
                                                      : (value >= threshold + hysteresis);
        if (released) latched = false;
        return false;
    }
    const bool condition = (cmp == Compare::Above) ? (value > threshold) : (value < threshold);
    if (!condition) return false;
    latched = true;
    return true;
}

uint8_t weekdayBit(int tmWday) {
    if (tmWday < 0 || tmWday > 6) return 0;
    if (tmWday == 0) return (uint8_t)(1u << 6);
    return (uint8_t)(1u << (tmWday - 1));
}

bool scheduleDue(uint8_t hour, uint8_t minute, uint8_t daysMask, int tmHour, int tmMin, int tmWday) {
    if (hour > 23 || minute > 59) return false;
    const uint8_t mask = daysMask ? daysMask : 0x7F;
    if ((mask & weekdayBit(tmWday)) == 0) return false;
    return tmHour == hour && tmMin == minute;
}

bool scheduleShouldFire(bool due, uint32_t todayKey, uint32_t lastFireDayKey) {
    return due && lastFireDayKey != todayKey;
}

} // namespace trigger_rules
