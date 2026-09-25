#pragma once

#include <Arduino.h>
#include "common/Pins.h"

class ModemUart {
public:
    using LineHandler = void(*)(void* ctx, const char* line);
    using ByteHandler = void(*)(void* ctx, char c);

    explicit ModemUart(HardwareSerial& serial) : _serial(serial) {}

    void setLineHandler(LineHandler h, void* ctx) {
        _handler = h;
        _handlerCtx = ctx;
    }

    void setByteHandler(ByteHandler h, void* ctx) {
        _byteHandler = h;
        _byteHandlerCtx = ctx;
    }

    void begin(uint32_t baud) { _serial.begin(baud, SERIAL_8N1, Pin::GSM_RX, Pin::GSM_TX); }

    // When enabled, disables line framing while still forwarding raw bytes to byte handler.
    // Needed for manual RX modes where binary payload follows AT headers.
    void setDataMode(bool en) { _dataMode = en; }
    bool isDataMode() const { return _dataMode; }

    // Poll RX and emit complete lines (without CR/LF).
    void pollRx();

    // TX helpers. Text AT lines are mirrored via logger.log; writeBytes payload is not.
    void writeRaw(const char* s);
    void writeLine(const char* line); // appends \r\n
    void writeBytes(const uint8_t* data, size_t len);
    void writeByte(uint8_t b);

    void flushInput();

private:
    HardwareSerial& _serial;
    LineHandler _handler{nullptr};
    void* _handlerCtx{nullptr};
    ByteHandler _byteHandler{nullptr};
    void* _byteHandlerCtx{nullptr};

    static constexpr size_t LINE_BUF_SIZE = 160;
    char _lineBuf[LINE_BUF_SIZE]{};
    size_t _lineLen{0};
    bool _dataMode{false};
};

