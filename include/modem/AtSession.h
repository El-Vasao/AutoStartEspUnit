#pragma once

#include <Arduino.h>
#include "common/Constants.h"
#include "modem/ModemUart.h"

/**
 * @brief Неблокирующие очереди AT-команд на ModemUart.
 *
 * Две очереди (обычная и с высоким приоритетом). Слот копирует команду в
 * `cmdBuf`, чтобы вызывающий не держал живой указатель. См. docs/modules/modem.md.
 */
class AtSession {
public:
    enum class Expect : uint8_t {
        None = 0,
        Ok = 1 << 0,
        Prompt = 1 << 1, // приглашение '>' (CIPSEND)
        LinePrefix = 1 << 2, // любая строка, начинающаяся с префикса
        AnyLine = 1 << 3,
    };

    struct Request {
        const char* cmd;           // команда без CRLF (после постановки в очередь указывает на cmdBuf)
        uint32_t timeoutMs;
        uint8_t expectMask;        // битовая маска Expect
        const char* prefix;        // для LinePrefix
        const char* tag;           // необязательно (для логов уровнем выше)
        char cmdBuf[Gsm::CMD_BUFFER_SIZE]{};
    };

    struct Result {
        bool ok{false};
        bool error{false};
        bool timedOut{false};
        bool gotPrompt{false};
        bool gotPrefix{false};
        bool hasCapturedLine{false};
        char capturedLine[64]{};
        const char* tag{nullptr}; // тег завершённого запроса (не владеет)
    };

    explicit AtSession(ModemUart& uart) : _uart(uart) {}

    ModemUart& uart() { return _uart; }

    void reset();

    // Поставить AT-запрос в очередь (неблокирующе). false, если очередь полна.
    bool enqueue(const Request& r);
    bool enqueueHigh(const Request& r);
    uint8_t normalQueueFree() const { return (uint8_t)(QSIZE - _qCount); }

    // Двигать работу: в простое послать следующую команду из очереди и проверить таймаут.
    void tick(uint32_t nowMs);

    // Точки входа RX от ModemUart
    void onLine(const char* line);
    void onByte(char c);

    bool isBusy() const { return _state != State::Idle; }
    bool hasResult() const { return _state == State::Done; }
    Result takeResult(); // возврат в Idle

    const Request* activeRequest() const { return _state == State::Waiting ? &_active : nullptr; }

private:
    enum class State : uint8_t { Idle, Waiting, Done };
    State _state{State::Idle};

    ModemUart& _uart;

    static constexpr uint8_t QSIZE = Gsm::AT_QSIZE;
    static constexpr uint8_t HQSIZE = Gsm::AT_HQSIZE;
    Request _q[QSIZE]{};
    uint8_t _qHead{0};
    uint8_t _qCount{0};
    Request _hq[HQSIZE]{};
    uint8_t _hqHead{0};
    uint8_t _hqCount{0};

    Request _active{};
    uint32_t _deadlineMs{0};
    Result _res{};
    /// Приглашение `>` только в начале строки (после CR/LF или старта ожидания).
    bool _promptLineStart{true};

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

