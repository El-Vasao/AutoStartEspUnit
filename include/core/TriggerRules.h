// include/core/TriggerRules.h
#pragma once

#include <stdint.h>

/**
 * Правила срабатывания триггеров и термостата (без Arduino, тестируются на хосте).
 * Менеджеры в src/core/*Manager.cpp только читают входы/датчики и вызывают эти правила.
 * См. docs/modules/core.md.
 */
namespace trigger_rules {

enum class Compare : uint8_t { Above, Below, Invalid };

/// "above" / "below"; всё остальное — Invalid (условие никогда не выполняется).
Compare compareFromString(const char* s);

/// Срабатывание по фронту: true один раз, когда условие стало истинным; снова — после того,
/// как условие побывало ложным.
struct EdgeLatch {
    bool latched{false};
    bool update(bool condition);
    void reset() { latched = false; }
    /// Взвести без срабатывания (boot / возврат после OTA при уже активном уровне).
    void seed(bool condition) {
        if (condition) latched = true;
        else latched = false;
    }
};

/**
 * Фронт температурного порога с гистерезисом отпускания: срабатывает, когда значение пересекло порог,
 * и снова взводится, только когда значение вернулось за порог на `hysteresis`. Шум датчика около
 * порога не перезапускает программу.
 */
struct ThresholdLatch {
    bool latched{false};
    bool update(float value, float threshold, Compare cmp, float hysteresis);
    void reset() { latched = false; }
    /// Взвести без срабатывания, если значение уже за порогом.
    void seed(float value, float threshold, Compare cmp) {
        if (cmp == Compare::Invalid) {
            latched = false;
            return;
        }
        const bool condition = (cmp == Compare::Above) ? (value > threshold) : (value < threshold);
        latched = condition;
    }
};

} // namespace trigger_rules
