#include "modem/ModemUart.h"

#include "common/Logger.h"

#include <string.h>

void ModemUart::emitFramedLine_(char* line) {
    if (!line || !line[0]) return;
    // Whitespace-only modem noise (often before SEND OK) — skip log and handler.
    bool onlyWs = true;
    for (const char* p = line; *p; ++p) {
        if (*p != ' ' && *p != '\t') {
            onlyWs = false;
            break;
        }
    }
    if (onlyWs) return;
    // Skip modem echo of the last TX command (ATE1); ATE0 is preferred, this is safety.
    if (_lastTx[0] && strcmp(line, _lastTx) == 0) {
        if (_handler) _handler(_handlerCtx, line);
        return;
    }
    logger.log("[AT] << %s\n", line);
    if (_handler) _handler(_handlerCtx, line);
}

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

void ModemUart::flushTx() {
    _serial.flush();
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
    strlcpy(_lastTx, line, sizeof(_lastTx));
    // Text control-plane only; CIPSEND body uses writeBytes (not logged).
    logger.log("[AT] >> %s\n", line);
    // No Serial.flush(): blocks long enough to trip WDT under AT load.
}

void ModemUart::writeBytes(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    // Chunk CIPSEND payload and poll RX between chunks. A single large write
    // (status full ~400B) blocks UART long enough for the HW RX FIFO to overflow
    // mid-+IPD → MQTT PUBLISH truncated (rx_incomplete have≪need).
    // Payload bytes are intentionally not logged.
    constexpr size_t kChunk = 32;
    size_t off = 0;
    while (off < len) {
        const size_t n = ((len - off) > kChunk) ? kChunk : (len - off);
        _serial.write(data + off, n);
        off += n;
        pollRx();
        yield();
    }
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
            // Binary +IPD / payload path: no line framing, no AT log.
            if ((++n & 0x3F) == 0) {
                yield();
            }
            continue;
        }
        if (c == '\r') continue;
        if (c == '\n') {
            if (_lineLen == 0) continue;
            _lineBuf[_lineLen] = '\0';
            emitFramedLine_(_lineBuf);
            _lineLen = 0;
            continue;
        }

        // CIPSEND prompt arrives without CRLF — log clearly and do not buffer (avoids
        // attaching TX-echo / binary to '>' and emitting garbage "lines").
        if (c == '>' && _lineLen == 0) {
            logger.log("[AT] << CIPSEND prompt\n");
            if ((++n & 0x3F) == 0) {
                yield();
            }
            continue;
        }

        if (_lineLen + 1 >= LINE_BUF_SIZE) {
            // Truncate the line to keep framing intact.
            _lineBuf[_lineLen] = '\0';
            emitFramedLine_(_lineBuf);
            _lineLen = 0;
        }
        _lineBuf[_lineLen++] = c;

        // Avoid WDT resets when modem dumps bursts (e.g. RX payload, URCs).
        if ((++n & 0x3F) == 0) {
            yield();
        }
    }
}
