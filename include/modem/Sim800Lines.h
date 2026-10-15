// include/modem/Sim800Lines.h
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * Чистые помощники TCP-транспорта SIM800 (без Arduino, тестируются на хосте).
 * Используются Sim800TcpTransport; описание потока байтов — docs/modules/modem.md.
 */
namespace sim800_lines {

/**
 * Строка UART, оканчивающаяся CRLF, — управляющий текст модема (OK, URC, CLOSED, эхо AT ...)?
 * Такие строки не попадают в поток TCP/MQTT. Хвостовые CR/LF/пробелы не учитываются.
 */
bool isControlLine(const char* s, size_t len);

/// Побайтовый разбор `+IPD,<len>:<payload>` (AT+CIPHEAD=1).
class IpdFramer {
public:
    enum class Event : uint8_t {
        Consumed,   ///< байт заголовка, ничего делать не нужно
        Raw,        ///< не +IPD: подать replay(), затем текущий байт в текстовый путь
        DataStart,  ///< ':' после длины: дальше length() байт нагрузки (режим данных UART)
        Payload,    ///< байт нагрузки, будут ещё
        PayloadEnd, ///< последний байт нагрузки
    };
    enum class State : uint8_t { Idle, MatchI, MatchP, MatchD, MatchComma, ReadLen, ReadData };

    static constexpr uint16_t kMaxIpdLen = 1024; // Sim800Tcp::TCP_BUF_SIZE / MQTT_FRAME_MAX

    Event feed(char c);
    void reset();

    /// Байты, съеденные прерванным заголовком; действительно после Event::Raw.
    const char* replay() const { return _replay; }
    State state() const { return _state; }
    uint16_t length() const { return _len; }
    uint16_t received() const { return _read; }
    bool inPayload() const { return _state == State::ReadData; }

private:
    static constexpr uint8_t kMaxDigits = 5;

    State _state{State::Idle};
    uint16_t _len{0};
    uint16_t _read{0};
    char _digits[kMaxDigits + 1]{};
    uint8_t _digitCount{0};
    char _replay[sizeof("+IPD,") + kMaxDigits]{};

    Event abort_();
};

} // namespace sim800_lines
