#pragma once

#include <Arduino.h>
#include "common/Constants.h"
#include "modem/ModemUart.h"

class AtSession {
public:
    enum class Expect : uint8_t {
        None = 0,
        Ok = 1 << 0,
        Prompt = 1 << 1, // '>' prompt (CIPSEND)
        LinePrefix = 1 << 2, // any line starting with prefix
        AnyLine = 1 << 3,
    };

    struct Request {
        const char* cmd;           // command without CRLF
        uint32_t timeoutMs;
        uint8_t expectMask;        // Expect bitmask
        const char* prefix;        // for LinePrefix
        const char* tag;           // optional (for higher-level logs)
    };

    struct Result {
        bool ok{false};
        bool error{false};
        bool timedOut{false};
        bool gotPrompt{false};
        bool gotPrefix{false};
        bool hasCapturedLine{false};
        char capturedLine[64]{};
        const char* tag{nullptr}; // tag of the completed request (non-owning)
    };

    explicit AtSession(ModemUart& uart) : _uart(uart) {}

    ModemUart& uart() { return _uart; }

    void reset();

    // Queue an AT request (non-blocking). Returns false if queue full.
    bool enqueue(const Request& r);
    bool enqueueHigh(const Request& r);

    // Drive progress: send next queued command if idle, check timeout.
    void tick(uint32_t nowMs);

    // RX hooks from ModemUart
    void onLine(const char* line);
    void onByte(char c);

    bool isBusy() const { return _state != State::Idle; }
    bool hasResult() const { return _state == State::Done; }
    Result takeResult(); // moves back to Idle

    const Request* activeRequest() const { return _state == State::Waiting ? &_active : nullptr; }

private:
    enum class State : uint8_t { Idle, Waiting, Done };
    State _state{State::Idle};

    ModemUart& _uart;

    static constexpr uint8_t QSIZE = GSM::AT_QSIZE;
    static constexpr uint8_t HQSIZE = GSM::AT_HQSIZE;
    Request _q[QSIZE]{};
    uint8_t _qHead{0};
    uint8_t _qCount{0};
    Request _hq[HQSIZE]{};
    uint8_t _hqHead{0};
    uint8_t _hqCount{0};

    Request _active{};
    uint32_t _deadlineMs{0};
    Result _res{};

    void startNext_(uint32_t nowMs);
    void finish_(bool ok, bool err, bool timeout);
    bool enqueue_(Request* q, uint8_t qsize, uint8_t& head, uint8_t& count, const Request& r);
};

static inline uint8_t atExpectMask(AtSession::Expect e) {
    return (uint8_t)e;
}

static inline uint8_t atExpectOr(uint8_t a, AtSession::Expect b) {
    return (uint8_t)(a | (uint8_t)b);
}

