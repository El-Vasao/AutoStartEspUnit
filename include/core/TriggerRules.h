// include/core/TriggerRules.h
#pragma once

#include <stdint.h>

/**
 * Правила срабатывания триггеров, термостата и расписания (без Arduino, тестируются на хосте).
 * Менеджеры в src/core/*Manager.cpp только читают входы/датчики/время и вызывают эти правила.
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
};

/// Бит дня недели в маске расписания (bit0 = пн … bit6 = вс) для `tm_wday` (0 = вс); 0 вне диапазона.
uint8_t weekdayBit(int tmWday);

/// Наступила ли минута расписания: время совпало, день разрешён маской (0 = каждый день).
bool scheduleDue(uint8_t hour, uint8_t minute, uint8_t daysMask, int tmHour, int tmMin, int tmWday);

/// Запускать ли программу расписания: минута наступила, а сегодня ещё не было успешного запуска.
bool scheduleShouldFire(bool due, uint32_t todayKey, uint32_t lastFireDayKey);

} // namespace trigger_rules
