// src/io/RpmEstimator.cpp
#include "io/RpmEstimator.h"

namespace {
constexpr uint32_t ZERO_TIMEOUT_US = Rpm::ZERO_TIMEOUT_MS * 1000UL;
}

void RpmEstimator::configure(uint16_t edgesPerRev) {
    edgesPerRev_ = edgesPerRev ? edgesPerRev : 1;
    minIntervalUs_ = 60000000UL / (Rpm::MAX_RPM * static_cast<uint32_t>(edgesPerRev_));
    reset();
}

void RpmEstimator::reset() {
    head_ = 0;
    count_ = 0;
    suspectStreak_ = 0;
}

void IRAM_ATTR RpmEstimator::onEdge(uint32_t nowUs) {
    if (count_ == 0) {
        ts_[0] = nowUs;
        head_ = 1;
        count_ = 1;
        return;
    }
    const uint32_t last = ts_[(head_ - 1) & MASK];
    const uint32_t dt = nowUs - last;
    if (dt > ZERO_TIMEOUT_US) {
        ts_[0] = nowUs;
        head_ = 1;
        count_ = 1;
        suspectStreak_ = 0;
        return;
    }

    bool suspect = dt < minIntervalUs_;
    if (!suspect && count_ >= 3) {
        const uint32_t prev1 = ts_[(head_ - 2) & MASK];
        const uint32_t prev2 = ts_[(head_ - 3) & MASK];
        const uint32_t full = nowUs - prev1;
        const uint32_t prevFull = last - prev2;
        suspect = full < prevFull / 4;
    }

    if (suspect) {
        if (++suspectStreak_ > Rpm::MAX_SUSPECT_STREAK) {
            ts_[0] = nowUs;
            head_ = 1;
            count_ = 1;
            suspectStreak_ = 0;
            return;
        }
        head_ = (head_ - 1) & MASK;
        count_--;
        return;
    }

    suspectStreak_ = 0;
    ts_[head_] = nowUs;
    head_ = (head_ + 1) & MASK;
    if (count_ < Rpm::RING) count_++;
}

float RpmEstimator::rpm(uint32_t nowUs) const {
    if (count_ < 2) return 0.0f;
    const uint32_t newest = ts_[(head_ - 1) & MASK];
    if (nowUs - newest > ZERO_TIMEOUT_US) return 0.0f;

    uint8_t k = Rpm::RING - 1;
    if (edgesPerRev_ < k) k = static_cast<uint8_t>(edgesPerRev_);
    if (count_ - 1 < k) return 0.0f;

    const uint32_t oldest = ts_[(head_ - 1 - k) & MASK];
    const uint32_t span = newest - oldest;
    if (span == 0) return 0.0f;
    return 60000000.0f * static_cast<float>(k) / (static_cast<float>(edgesPerRev_) * static_cast<float>(span));
}
