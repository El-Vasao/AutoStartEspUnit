// include/core/ErrorManager.h
#pragma once

#include <Arduino.h>
#include "common/Constants.h"
#include "common/ErrorCodes.h"

/**
 * One slot in the error history ring (RAM; mirrored to RTC without `delivered`).
 */
struct ErrorHistoryEntry {
    ErrorCode code{ErrorCode::NONE};
    uint32_t uptimeSec{0};
    bool active{false};
    bool delivered{false};
    bool fromRtc{false};
};

/**
 * Wire/snapshot POD for one undelivered error (no ErrorCodes.h dependency beyond uint8).
 */
struct ErrorSnapshotEntry {
    uint8_t code{0};
    bool active{false};
    char msg[ErrorHistory::MSG_MAX]{};
    uint32_t uptimeSec{0}; ///< fingerprint for markDelivered
};

/**
 * RTC block: full history ring + current active code.
 * Survives soft reset; not reliable across full power loss.
 */
struct RtcErrorBlock {
    uint32_t magic;
    uint8_t count;
    ErrorCode current;
    uint32_t currentUptime;
    struct PackedEntry {
        ErrorCode code;
        uint32_t uptimeSec;
        uint8_t flags; ///< bit0 = active
    } entries[ErrorHistory::CAPACITY];
    uint16_t crc;
} __attribute__((aligned(4)));

/**
 * Error manager: current error + history ring + RTC persist + MQTT delivery flags.
 */
class ErrorManager {
public:
    ErrorManager();

    void set(ErrorCode err);
    void clear();

    /// Boot-only: always append undelivered fact (does not erase prior RTC history).
    void recordBootReset(ErrorCode err);

    ErrorCode get() const { return _lastError; }
    const char* getMessage() const { return errorCodeToString(_lastError); }
    uint32_t getTime() const { return _errorTime; }

    void loadFromRtc();
    void saveToRtc();
    void clearRtc();

    /// Newest-first undelivered copy for MQTT status. Returns count written.
    uint8_t copyUndelivered(ErrorSnapshotEntry* out, uint8_t cap) const;

    /// Mark matching entries delivered after successful status PUBLISH stage.
    void markDelivered(const ErrorSnapshotEntry* sent, uint8_t n);

    uint8_t historyCount() const { return _count; }
    const ErrorHistoryEntry& historyAt(uint8_t i) const; ///< i=0 newest

private:
    ErrorCode _lastError;
    uint32_t _errorTime;

    ErrorHistoryEntry _ring[ErrorHistory::CAPACITY]{};
    uint8_t _count{0}; ///< valid entries, newest at index 0

    void pushFront_(ErrorHistoryEntry e);
    void deactivateActives_();
    void persist_();

    static constexpr uint32_t RTC_MAGIC = 0xE11A70CCu; ///< bumped vs single-record layout
};
