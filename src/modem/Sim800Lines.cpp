// src/modem/Sim800Lines.cpp
#include "modem/Sim800Lines.h"

#include <string.h>

namespace sim800_lines {

bool isControlLine(const char* s, size_t len) {
    if (!s || len == 0) return true;
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' || s[len - 1] == ' ' || s[len - 1] == '\t')) {
        len--;
    }
    if (len == 0) return true;

    if (len >= 2 && s[0] == 'A' && s[1] == 'T') return true;       // "AT...", "ATE1", ...
    if (len == 2 && s[0] == 'O' && s[1] == 'K') return true;        // "OK"
    if (len >= 5 && memcmp(s, "ERROR", 5) == 0) return true;        // "ERROR"
    if (len == 3 && memcmp(s, "RDY", 3) == 0) return true;
    if (len == 4 && memcmp(s, "RING", 4) == 0) return true;
    if (len == 4 && memcmp(s, "BUSY", 4) == 0) return true;
    if (len == 6 && memcmp(s, "CLOSED", 6) == 0) return true;
    if (len == 10 && memcmp(s, "NO CARRIER", 10) == 0) return true;
    if (len == 9 && memcmp(s, "NO ANSWER", 9) == 0) return true;
    if (len == 10 && memcmp(s, "Call Ready", 10) == 0) return true;
    if (len == 9 && memcmp(s, "SMS Ready", 9) == 0) return true;
    if (len >= 10 && memcmp(s, "CONNECT OK", 10) == 0) return true;
    if (len >= 12 && memcmp(s, "CONNECT FAIL", 12) == 0) return true;
    if (len >= 15 && memcmp(s, "ALREADY CONNECT", 15) == 0) return true;
    if (len >= 8 && memcmp(s, "CLOSE OK", 8) == 0) return true;
    if (len >= 7 && memcmp(s, "SHUT OK", 7) == 0) return true;
    if (len >= 7 && memcmp(s, "SEND OK", 7) == 0) return true;
    if (len >= 9 && memcmp(s, "SEND FAIL", 9) == 0) return true;
    if (s[0] == '>') return true;                                   // CIPSEND prompt

    // URCs start with '+'; also stripped forms if the +IPD matcher ate '+'.
    if (s[0] == '+') return true;
    if (len >= 4 && memcmp(s, "CSQ:", 4) == 0) return true;
    if (len >= 5 && memcmp(s, "CREG:", 5) == 0) return true;
    if (len >= 5 && memcmp(s, "COPS:", 5) == 0) return true;
    if (len >= 4 && memcmp(s, "IPD,", 4) == 0) return true;
    return false;
}

void IpdFramer::reset() {
    _state = State::Idle;
    _len = 0;
    _read = 0;
    _digitCount = 0;
    _digits[0] = '\0';
    _replay[0] = '\0';
}

IpdFramer::Event IpdFramer::abort_() {
    const char* prefix = "";
    switch (_state) {
        case State::MatchI:     prefix = "+"; break;
        case State::MatchP:     prefix = "+I"; break;
        case State::MatchD:     prefix = "+IP"; break;
        case State::MatchComma: prefix = "+IPD"; break;
        case State::ReadLen:    prefix = "+IPD,"; break;
        default: break;
    }
    strncpy(_replay, prefix, sizeof(_replay) - 1);
    _replay[sizeof(_replay) - 1] = '\0';
    if (_state == State::ReadLen) {
        strncat(_replay, _digits, sizeof(_replay) - strlen(_replay) - 1);
    }
    _state = State::Idle;
    _len = 0;
    _read = 0;
    _digitCount = 0;
    _digits[0] = '\0';
    return Event::Raw;
}

IpdFramer::Event IpdFramer::feed(char c) {
    _replay[0] = '\0';
    switch (_state) {
        case State::Idle:
            if (c == '+') {
                _state = State::MatchI;
                return Event::Consumed;
            }
            return Event::Raw;
        case State::MatchI:
            if (c == 'I') {
                _state = State::MatchP;
                return Event::Consumed;
            }
            return abort_();
        case State::MatchP:
            if (c == 'P') {
                _state = State::MatchD;
                return Event::Consumed;
            }
            return abort_();
        case State::MatchD:
            if (c == 'D') {
                _state = State::MatchComma;
                return Event::Consumed;
            }
            return abort_();
        case State::MatchComma:
            if (c == ',') {
                _len = 0;
                _read = 0;
                _digitCount = 0;
                _digits[0] = '\0';
                _state = State::ReadLen;
                return Event::Consumed;
            }
            return abort_();
        case State::ReadLen:
            // Some firmwares insert spaces/CRLF around the length.
            if (c == ' ' || c == '\r' || c == '\n' || c == '\t') return Event::Consumed;
            if (c >= '0' && c <= '9') {
                const uint32_t v = (uint32_t)_len * 10UL + (uint32_t)(c - '0');
                _len = (v > (uint32_t)kMaxIpdLen) ? kMaxIpdLen : (uint16_t)v;
                if (_digitCount < kMaxDigits) {
                    _digits[_digitCount++] = c;
                    _digits[_digitCount] = '\0';
                }
                return Event::Consumed;
            }
            if (c == ':') {
                _read = 0;
                if (_len == 0) {
                    _state = State::Idle;
                    return Event::DataStart;
                }
                _state = State::ReadData;
                return Event::DataStart;
            }
            return abort_();
        case State::ReadData:
            if (++_read >= _len) {
                _state = State::Idle;
                return Event::PayloadEnd;
            }
            return Event::Payload;
    }
    _state = State::Idle;
    return Event::Raw;
}

} // namespace sim800_lines
