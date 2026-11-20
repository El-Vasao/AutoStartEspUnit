#include "modem/ModemUart.h"

#include "common/Logger.h"

#include <string.h>

void ModemUart::emitFramedLine_(char* line) {
    if (!line || !line[0]) return;
    // Шум модема из одних пробелов (часто перед SEND OK) — не писать в лог и не звать обработчик.
    bool onlyWs = true;
    for (const char* p = line; *p; ++p) {
        if (*p != ' ' && *p != '\t') {
            onlyWs = false;
            break;
        }
    }
    if (onlyWs) return;
    // Пропустить эхо модема последней команды TX (ATE1); предпочтителен ATE0, это страховка.
    if (_lastTx[0] && strcmp(line, _lastTx) == 0) {
        return;
    }
    logger.log(LogModule::AT, "<< %s\n", line);
    if (_handler) _handler(_handlerCtx, line);
}

void ModemUart::flushInput() {
    uint16_t n = 0;
    while (_serial.available()) {
        (void)_serial.read();
        // Кормить WDT, даже если модем вываливает много байт.
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
    // Без Serial.flush(): под нагрузкой AT блокировка достаточно длинная, чтобы сработал WDT.
}

void ModemUart::writeLine(const char* line) {
    if (!line) line = "";
    _serial.print(line);
    _serial.print("\r\n");
    strlcpy(_lastTx, line, sizeof(_lastTx));
    // Только текстовая плоскость управления; тело CIPSEND идёт через writeBytes (в лог не пишется).
    logger.log(LogModule::AT, ">> %s\n", line);
    // Без Serial.flush(): под нагрузкой AT блокировка достаточно длинная, чтобы сработал WDT.
}

void ModemUart::writeBytes(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    // Резать нагрузку CIPSEND на куски и между ними опрашивать RX. Одна большая запись
    // (полный status ~400 Б) держит UART так долго, что аппаратный FIFO RX переполняется
    // посреди +IPD → PUBLISH MQTT обрезан (rx_incomplete have≪need).
    // Байты нагрузки в лог намеренно не пишутся.
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
            // Двоичный путь +IPD / нагрузки: без кадрирования строк и без лога AT.
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

        // Приглашение CIPSEND приходит без CRLF — ясно писать в лог и не копить (иначе
        // к '>' прилипнут эхо TX / двоичное и наружу выйдут мусорные «строки»).
        if (c == '>' && _lineLen == 0) {
            logger.log(LogModule::AT, "<< CIPSEND prompt\n");
            if ((++n & 0x3F) == 0) {
                yield();
            }
            continue;
        }

        if (_lineLen + 1 >= LINE_BUF_SIZE) {
            // Обрезать строку, чтобы кадрирование не разъехалось.
            _lineBuf[_lineLen] = '\0';
            emitFramedLine_(_lineBuf);
            _lineLen = 0;
        }
        _lineBuf[_lineLen++] = c;

        // Не давать WDT сбросить плату, когда модем вываливает пачки (например, нагрузка RX, URC).
        if ((++n & 0x3F) == 0) {
            yield();
        }
    }
}
