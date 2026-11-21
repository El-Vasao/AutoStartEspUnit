#pragma once

#include <Arduino.h>
#include "common/Constants.h"
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

    // Когда включено, кадрирование строк выключено, а сырые байты всё ещё идут в обработчик байт.
    // Нужно для ручных режимов RX, где за заголовками AT идёт двоичная нагрузка.
    void setDataMode(bool en) { _dataMode = en; }
    bool isDataMode() const { return _dataMode; }

    // Опросить RX и отдать целые строки (без CR/LF).
    void pollRx();

    // Помощники TX. Текстовые строки AT зеркалируются через logger.log; нагрузка writeBytes — нет.
    void writeRaw(const char* s);
    void writeLine(const char* line); // дописывает \r\n
    void writeBytes(const uint8_t* data, size_t len);
    void writeByte(uint8_t b);

    void flushInput();
    /// Сбросить недособранную строку (после обрыва TCP / сброса +IPD).
    void clearPartialLine() { _lineLen = 0; }
    /// Опустошить TX UART (смена скорости / IPR NV); это лучше сырого Serial.flush().
    void flushTx();

private:
    HardwareSerial& _serial;
    LineHandler _handler{nullptr};
    void* _handlerCtx{nullptr};
    ByteHandler _byteHandler{nullptr};
    void* _byteHandlerCtx{nullptr};

    static constexpr size_t LINE_BUF_SIZE = Gsm::UART_LINE_BUF;
    char _lineBuf[LINE_BUF_SIZE]{};
    size_t _lineLen{0};
    bool _dataMode{false};

    /// Текст последнего writeLine (без CRLF), чтобы не дублировать эхо, если эхо модема включено.
    static constexpr size_t LAST_TX_SIZE = Gsm::CMD_BUFFER_SIZE;
    char _lastTx[LAST_TX_SIZE]{};

    void emitFramedLine_(char* line);
};

