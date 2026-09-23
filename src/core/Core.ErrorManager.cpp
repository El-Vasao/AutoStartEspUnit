// src/core/Core.ErrorManager.cpp
#include "core/ErrorManager.h"
#include "common/Utils.h"
#include "common/Logger.h"

#include <string.h>

/**
 * @file Core.ErrorManager.cpp
 * @brief Error history ring + RTC persist + MQTT one-shot delivery flags.
 */

namespace {
RTC_DATA_ATTR RtcErrorBlock s_rtcErrorBlock;
}

ErrorManager::ErrorManager() : _lastError(ErrorCode::NONE), _errorTime(0) {}

void ErrorManager::pushFront_(ErrorHistoryEntry e) {
    if (_count < ErrorHistory::CAPACITY) {
        for (uint8_t i = _count; i > 0; i--) {
            _ring[i] = _ring[i - 1];
        }
        _count++;
    } else {
        for (uint8_t i = ErrorHistory::CAPACITY - 1; i > 0; i--) {
            _ring[i] = _ring[i - 1];
        }
    }
    _ring[0] = e;
}

void ErrorManager::deactivateActives_() {
    for (uint8_t i = 0; i < _count; i++) {
        _ring[i].active = false;
    }
}

void ErrorManager::persist_() {
    saveToRtc();
}

void ErrorManager::set(ErrorCode err) {
    if (err == ErrorCode::NONE) {
        clear();
        return;
    }

    const uint32_t nowSec = millis() / Time::MS_PER_SEC;

    // Same code already active — refresh uptime, keep undelivered if not yet sent.
    if (_lastError == err && _count > 0 && _ring[0].active && _ring[0].code == err) {
        _ring[0].uptimeSec = nowSec;
        _errorTime = nowSec;
        persist_();
        return;
    }

    deactivateActives_();
    ErrorHistoryEntry e{};
    e.code = err;
    e.uptimeSec = nowSec;
    e.active = true;
    e.delivered = false;
    e.fromRtc = false;
    pushFront_(e);

    _lastError = err;
    _errorTime = nowSec;
    logger.log("[ErrorManager] ERROR: %s [%d]\n", errorCodeToString(err), (int)err);
    persist_();
}

void ErrorManager::clear() {
    deactivateActives_();
    _lastError = ErrorCode::NONE;
    _errorTime = 0;
    // Keep ring history for MQTT one-shot; do not wipe RTC ring.
    persist_();
    logger.log("[ErrorManager] Cleared (history kept)\n");
}

void ErrorManager::recordBootReset(ErrorCode err) {
    if (err == ErrorCode::NONE) return;

    const uint32_t nowSec = millis() / Time::MS_PER_SEC;
    const bool makeActive = (_lastError == ErrorCode::NONE);

    ErrorHistoryEntry e{};
    e.code = err;
    e.uptimeSec = nowSec;
    e.active = makeActive;
    e.delivered = false;
    e.fromRtc = true; // deliver with first full alongside RTC restores

    pushFront_(e);

    if (makeActive) {
        _lastError = err;
        _errorTime = nowSec;
    }

    logger.log("[ErrorManager] BOOT RESET: %s [%d] active=%u\n", errorCodeToString(err), (int)err,
               (unsigned)makeActive);
    persist_();
}

const ErrorHistoryEntry& ErrorManager::historyAt(uint8_t i) const {
    static const ErrorHistoryEntry kEmpty{};
    if (i >= _count) return kEmpty;
    return _ring[i];
}

uint8_t ErrorManager::copyUndelivered(ErrorSnapshotEntry* out, uint8_t cap) const {
    if (!out || cap == 0) return 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < _count && n < cap; i++) {
        if (_ring[i].delivered) continue;
        if (_ring[i].code == ErrorCode::NONE) continue;
        out[n].code = static_cast<uint8_t>(_ring[i].code);
        out[n].active = _ring[i].active;
        out[n].uptimeSec = _ring[i].uptimeSec;
        strlcpy(out[n].msg, errorCodeToString(_ring[i].code), sizeof(out[n].msg));
        n++;
    }
    return n;
}

void ErrorManager::markDelivered(const ErrorSnapshotEntry* sent, uint8_t n) {
    if (!sent || n == 0) return;
    for (uint8_t s = 0; s < n; s++) {
        for (uint8_t i = 0; i < _count; i++) {
            if (_ring[i].delivered) continue;
            if (static_cast<uint8_t>(_ring[i].code) != sent[s].code) continue;
            if (_ring[i].uptimeSec != sent[s].uptimeSec) continue;
            _ring[i].delivered = true;
            break;
        }
    }
    // delivered is RAM-only; no RTC write required
}

void ErrorManager::loadFromRtc() {
    RtcErrorBlock record = s_rtcErrorBlock;

    if (record.magic != RTC_MAGIC) return;

    uint16_t crc = calculateCRC16((const uint8_t*)&record, sizeof(record) - sizeof(record.crc));
    if (crc != record.crc) return;

    _count = record.count;
    if (_count > ErrorHistory::CAPACITY) _count = ErrorHistory::CAPACITY;

    for (uint8_t i = 0; i < _count; i++) {
        _ring[i] = ErrorHistoryEntry{};
        _ring[i].code = record.entries[i].code;
        _ring[i].uptimeSec = record.entries[i].uptimeSec;
        _ring[i].active = (record.entries[i].flags & 0x01u) != 0;
        _ring[i].delivered = false; // reboot → redeliver
        _ring[i].fromRtc = true;
    }
    for (uint8_t i = _count; i < ErrorHistory::CAPACITY; i++) {
        _ring[i] = ErrorHistoryEntry{};
    }

    _lastError = record.current;
    _errorTime = record.currentUptime;

    // Ensure active flags match current.
    if (_lastError == ErrorCode::NONE) {
        deactivateActives_();
    }

    logger.log("[ErrorManager] Loaded from RTC: current=%s count=%u\n", errorCodeToString(_lastError),
               (unsigned)_count);
}

void ErrorManager::saveToRtc() {
    RtcErrorBlock record{};
    record.magic = RTC_MAGIC;
    record.count = _count;
    record.current = _lastError;
    record.currentUptime = _errorTime;
    for (uint8_t i = 0; i < ErrorHistory::CAPACITY; i++) {
        if (i < _count) {
            record.entries[i].code = _ring[i].code;
            record.entries[i].uptimeSec = _ring[i].uptimeSec;
            record.entries[i].flags = _ring[i].active ? 0x01u : 0x00u;
        } else {
            record.entries[i] = {};
        }
    }
    record.crc = calculateCRC16((const uint8_t*)&record, sizeof(record) - sizeof(record.crc));
    s_rtcErrorBlock = record;
}

void ErrorManager::clearRtc() {
    memset(const_cast<RtcErrorBlock*>(&s_rtcErrorBlock), 0, sizeof(s_rtcErrorBlock));
}
