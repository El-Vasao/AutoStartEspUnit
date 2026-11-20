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

} // namespace trigger_rules
