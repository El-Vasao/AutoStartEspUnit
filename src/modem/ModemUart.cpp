#include "modem/ModemUart.h"

void ModemUart::flushInput() {
    uint16_t n = 0;
    while (_serial.available()) {
        (void)_serial.read();
        // Keep WDT fed even if modem dumps a lot of bytes.
        if ((++n & 0x3F) == 0) {
            yield();
        }
    }
}

void ModemUart::writeRaw(const char* s) {
    if (!s) return;
    _serial.print(s);
    // No Serial.flush(): blocks long enough to trip WDT under AT load.
}

void ModemUart::writeLine(const char* line) {
    if (!line) line = "";
    _serial.print(line);
    _serial.print("\r\n");
    // No Serial.flush(): blocks long enough to trip WDT under AT load.
}

void ModemUart::writeBytes(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    // Continuous CIPSEND burst; yield once after so SoftAP can run.
    _serial.write(data, len);
    yield();
}

void ModemUart::writeByte(uint8_t b) {
    _serial.write(b);
}

void ModemUart::pollRx() {
    uint16_t n = 0;
    while (_serial.available()) {
        const char c = (char)_serial.read();
        if (_byteHandler) _byteHandler(_byteHandlerCtx, c);
        if (_dataMode) {
            if ((++n & 0x3F) == 0) {
                yield();
            }
            continue;
        }
        if (c == '\r') continue;
        if (c == '\n') {
            if (_lineLen == 0) continue;
            _lineBuf[_lineLen] = '\0';
            if (_handler) _handler(_handlerCtx, _lineBuf);
            _lineLen = 0;
            continue;
        }

        if (_lineLen + 1 >= LINE_BUF_SIZE) {
            // Truncate the line to keep framing intact.
            _lineBuf[_lineLen] = '\0';
            if (_handler) _handler(_handlerCtx, _lineBuf);
            _lineLen = 0;
        }
        _lineBuf[_lineLen++] = c;

        // Avoid WDT resets when modem dumps bursts (e.g. RX payload, URCs).
        if ((++n & 0x3F) == 0) {
            yield();
        }
    }
}
