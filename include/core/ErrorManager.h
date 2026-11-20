// include/core/ErrorManager.h
#pragma once

#include <Arduino.h>
#include "common/Constants.h"
#include "common/ErrorCodes.h"

/**
 * Одна ячейка кольца истории ошибок (RAM; в RTC зеркалится без `delivered`).
 */
struct ErrorHistoryEntry {
    ErrorCode code{ErrorCode::NONE};
    uint32_t uptimeSec{0};
    bool active{false};
    bool delivered{false};
    bool fromRtc{false};
};

/**
 * POD снимка/провода для одной недоставленной или активной ошибки (из ErrorCodes.h нужна только зависимость на уровне uint8).
 */
struct ErrorSnapshotEntry {
    uint8_t code{0};
    bool active{false};
    char msg[ErrorHistory::MSG_MAX]{};
    uint32_t uptimeSec{0}; ///< отпечаток для markDelivered
};

/**
 * Блок RTC: полное кольцо истории и текущий (самый свежий) активный код.
 * Переживает мягкий сброс; при полном пропадании питания ненадёжен.
 */
struct RtcErrorBlock {
    uint32_t magic;
    uint8_t count;
    ErrorCode current;
    uint32_t currentUptime;
    struct PackedEntry {
        ErrorCode code;
        uint32_t uptimeSec;
        uint8_t flags; ///< bit0 = активна
    } entries[ErrorHistory::CAPACITY];
    uint16_t crc;
} __attribute__((aligned(4)));

/**
 * Менеджер ошибок: несколько активных кодов, кольцо истории, сохранение в RTC и флаги доставки MQTT.
 */
class ErrorManager {
public:
    ErrorManager();

    void set(ErrorCode err);
    void clear();
    /// Снять только совпавший код; текущий пересчитать по оставшимся активным.
    void clear(ErrorCode code);

    /// Только на старте: всегда дописать недоставленный факт (прежнюю историю RTC не стирает).
    void recordBootReset(ErrorCode err);

    ErrorCode get() const { return _lastError; }
    const char* getMessage() const { return errorCodeToString(_lastError); }
    uint32_t getTime() const { return _errorTime; }
    bool isActive(ErrorCode code) const;

    void loadFromRtc();
    void saveToRtc();
    void clearRtc();

    /// Копия недоставленных, сначала самые новые, для статуса MQTT. Возвращает число записанных.
    uint8_t copyUndelivered(ErrorSnapshotEntry* out, uint8_t cap) const;

    /// Сейчас активные ошибки, сначала самые новые (для UI/SSE). Возвращает число записанных.
    uint8_t copyActive(ErrorSnapshotEntry* out, uint8_t cap) const;

    /// Пометить совпавшие записи доставленными после успешной стадии PUBLISH статуса.
    void markDelivered(const ErrorSnapshotEntry* sent, uint8_t n);

    uint8_t historyCount() const { return _count; }
    const ErrorHistoryEntry& historyAt(uint8_t i) const; ///< i=0 — самая новая

private:
    ErrorCode _lastError;
    uint32_t _errorTime;

    ErrorHistoryEntry _ring[ErrorHistory::CAPACITY]{};
    uint8_t _count{0}; ///< число годных записей, самая новая по индексу 0

    void pushFront_(ErrorHistoryEntry e);
    void deactivateActives_();
    void recomputeCurrent_();
    void persist_();

    static constexpr uint32_t RTC_MAGIC = 0xE11A70CCu; ///< увеличен относительно раскладки с одной записью
};
