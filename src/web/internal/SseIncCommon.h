#pragma once

#include <Arduino.h>
#include <cstring>
#include "common/Constants.h"
#include "web/internal/SseStatusPort.h"

namespace sse_inc_detail {

constexpr size_t kPayloadCap = JsonBytes::Web::SSE_STATUS_JSON_MAX;
extern char gSsePayload[kPayloadCap];

inline uint32_t fnv1a32(const uint8_t* data, size_t len) {
    constexpr uint32_t kFnvPrime = 16777619U;
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= kFnvPrime;
    }
    return hash;
}

/// Writes SSE JSON payloads into NUL-terminated capped buffer (`gSsePayload`).
class PayloadPrint : public Print {
public:
    explicit PayloadPrint() : buf_(gSsePayload), cap_(kPayloadCap), len_(0), truncated_(false) {}

    size_t write(uint8_t c) override {
        if (len_ + 1 >= cap_) {
            truncated_ = true;
            return 0;
        }
        buf_[len_++] = static_cast<char>(c);
        return 1;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        if (!buffer || size == 0) return 0;
        const size_t room = (cap_ > len_ + 1) ? (cap_ - 1 - len_) : 0;
        if (room == 0) {
            truncated_ = true;
            return 0;
        }
        const size_t n = (size < room) ? size : room;
        memcpy(buf_ + len_, buffer, n);
        len_ += n;
        if (n < size) truncated_ = true;
        return n;
    }

    size_t length() const { return len_; }
    bool truncated() const { return truncated_; }
    char* data() { return buf_; }

    bool seal() {
        if (len_ >= cap_) return false;
        buf_[len_] = '\0';
        return true;
    }

private:
    char* buf_;
    size_t cap_;
    size_t len_;
    bool truncated_;
};

inline void commaOut(Print& p, bool* needComma) {
    if (*needComma) p.print(',');
    *needComma = true;
}

inline void escapeJsonString(Print& p, const char* s) {
    if (!s) return;
    for (; *s; s++) {
        const char ch = *s;
        switch (ch) {
            case '\\':
                p.print("\\\\");
                break;
            case '"':
                p.print("\\\"");
                break;
            case '\n':
                p.print("\\n");
                break;
            case '\r':
                p.print("\\r");
                break;
            default:
                p.write(static_cast<uint8_t>(ch));
                break;
        }
    }
}

void emitRelayInputTempVoltageMaps(Print& p, bool* needComma, const SseStatusPort& st);
void emitHardwarePayload(Print& p, const SseStatusPort& st);
void emitRuntimePayload(Print& p, const SseStatusPort& st);
void emitProgramPayload(Print& p, const SseStatusPort& st);
void emitFlashPayload(Print& p, const SseStatusPort& st);
void emitClocksPayload(Print& p, const SseStatusPort& st);
void emitSnapshotPayload(Print& p, const SseStatusPort& st);

} // namespace sse_inc_detail
