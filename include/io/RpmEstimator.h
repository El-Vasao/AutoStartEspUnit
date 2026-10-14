// include/io/RpmEstimator.h
#pragma once

#include <stdint.h>

#include "common/Constants.h"

#if defined(__has_include)
#if __has_include(<esp_attr.h>)
#include <esp_attr.h>
#endif
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

/**
 * @brief Обороты по периоду между перепадами импульсного входа (без железа, тестируется на хосте).
 *
 * - `onEdge` вызывается из ISR на каждом CHANGE; `pulses_per_rev` = перепадов на оборот.
 * - Помеха обычно даёт пару близких перепадов: подозрительный перепад отбрасывается вместе с
 *   предыдущим принятым, так что пара гасится целиком и чётность фронтов не сбивается.
 *   Подозрительный — интервал короче предела `Rpm::MAX_RPM` или полный период (два интервала)
 *   короче четверти предыдущего полного периода.
 * - `rpm()` считается по последним `min(pulses_per_rev, RING-1)` интервалам; нет перепадов
 *   дольше `Rpm::ZERO_TIMEOUT_MS` — 0.
 * - `rpm()` вызывается на копии, снятой при запрещённых прерываниях.
 */
class RpmEstimator {
public:
    void configure(uint16_t edgesPerRev);
    void reset();

    void IRAM_ATTR onEdge(uint32_t nowUs);

    float rpm(uint32_t nowUs) const;

    uint8_t edgeCount() const { return count_; }

private:
    static constexpr uint8_t MASK = Rpm::RING - 1;
    static_assert((Rpm::RING & (Rpm::RING - 1)) == 0, "Rpm::RING must be a power of two");

    uint32_t ts_[Rpm::RING]{};
    uint8_t head_{0};
    uint8_t count_{0};
    uint8_t suspectStreak_{0};
    uint16_t edgesPerRev_{1};
    uint32_t minIntervalUs_{0};
};

/// Порог оборотов считается достигнутым, только если держится непрерывно `Rpm::CONFIRM_MS`.
class RpmConfirm {
public:
    void reset() {
        above_ = false;
        confirmed_ = false;
        aboveSinceMs_ = 0;
    }

    bool update(uint32_t nowMs, float rpm, uint16_t thresholdRpm) {
        if (thresholdRpm == 0 || rpm < static_cast<float>(thresholdRpm)) {
            above_ = false;
            confirmed_ = false;
            return false;
        }
        if (!above_) {
            above_ = true;
            aboveSinceMs_ = nowMs;
        }
        confirmed_ = (nowMs - aboveSinceMs_) >= Rpm::CONFIRM_MS;
        return confirmed_;
    }

    bool confirmed() const { return confirmed_; }

private:
    bool above_{false};
    bool confirmed_{false};
    uint32_t aboveSinceMs_{0};
};
