#include "modem/AtSession.h"

#include "common/Logger.h"

void AtSession::reset() {
    _state = State::Idle;
    _qHead = 0;
    _qCount = 0;
    _hqHead = 0;
    _hqCount = 0;
    _deadlineMs = 0;
    _res = Result{};
    _active = Request{};
}

bool AtSession::enqueue_(Request* q, uint8_t qsize, uint8_t& head, uint8_t& count, const Request& r) {
    if (count >= qsize) return false;
    const uint8_t idx = (uint8_t)((head + count) % qsize);
    q[idx] = r;
    count++;
    return true;
}

bool AtSession::enqueue(const Request& r) {
    return enqueue_(_q, QSIZE, _qHead, _qCount, r);
}

bool AtSession::enqueueHigh(const Request& r) {
    return enqueue_(_hq, HQSIZE, _hqHead, _hqCount, r);
}

void AtSession::startNext_(uint32_t nowMs) {
    if (_hqCount == 0 && _qCount == 0) return;
    if (_hqCount > 0) {
        _active = _hq[_hqHead];
        _hqHead = (uint8_t)((_hqHead + 1) % HQSIZE);
        _hqCount--;
    } else {
        _active = _q[_qHead];
        _qHead = (uint8_t)((_qHead + 1) % QSIZE);
        _qCount--;
    }

    _res = Result{};
    _deadlineMs = nowMs + (_active.timeoutMs ? _active.timeoutMs : 1);
    _state = State::Waiting;

    // TX text logged in ModemUart::writeLine.
    _uart.writeLine(_active.cmd ? _active.cmd : "");
}

void AtSession::finish_(bool ok, bool err, bool timeout) {
    _res.ok = ok;
    _res.error = err;
    _res.timedOut = timeout;
    _res.tag = _active.tag;
    _state = State::Done;

    // Wire RX is logged in ModemUart; only TIMEOUT has no on-wire response.
    if (!timeout) return;
    const char* tag = (_active.tag && _active.tag[0]) ? _active.tag : nullptr;
    const char* cmd = (_active.cmd && _active.cmd[0]) ? _active.cmd : "";
    if (tag) {
        logger.log("[AT] << TIMEOUT for %s (%s)\n", cmd, tag);
    } else {
        logger.log("[AT] << TIMEOUT for %s\n", cmd);
    }
}

void AtSession::tick(uint32_t nowMs) {
    if (_state == State::Idle) {
        startNext_(nowMs);
        return;
    }
    if (_state != State::Waiting) return;
    if ((int32_t)(nowMs - _deadlineMs) >= 0) {
        finish_(false, false, true);
        return;
    }
}

void AtSession::onByte(char c) {
    if (_state != State::Waiting) return;
    if ((_active.expectMask & (uint8_t)Expect::Prompt) != 0) {
        if (c == '>') {
            _res.gotPrompt = true;
            // Prompt is an immediate completion for CIPSEND prelude.
            finish_(true, false, false);
        }
    }
}

void AtSession::onLine(const char* line) {
    if (_state != State::Waiting) return;
    if (!line || !line[0]) return;

    if ((_active.expectMask & (uint8_t)Expect::LinePrefix) != 0) {
        if (_active.prefix && strncmp(line, _active.prefix, strlen(_active.prefix)) == 0) {
            _res.gotPrefix = true;
            if (!_res.hasCapturedLine) {
                strlcpy(_res.capturedLine, line, sizeof(_res.capturedLine));
                _res.hasCapturedLine = true;
            }
            // NOTE: do not finish here; many AT commands require final OK/ERROR after the prefix line.
        }
    }

    if ((_active.expectMask & (uint8_t)Expect::Ok) != 0) {
        // SIM800 CIP* final replies are often "CLOSE OK" / "SHUT OK", not bare "OK".
        if (strcmp(line, "OK") == 0 || strcmp(line, "CLOSE OK") == 0 ||
            strcmp(line, "SHUT OK") == 0) {
            finish_(true, false, false);
            return;
        }
        if (strcmp(line, "ERROR") == 0 || strstr(line, "+CME ERROR") != nullptr) {
            if (!_res.hasCapturedLine) {
                strlcpy(_res.capturedLine, line, sizeof(_res.capturedLine));
                _res.hasCapturedLine = true;
            }
            finish_(false, true, false);
            return;
        }
    }

    if ((_active.expectMask & (uint8_t)Expect::AnyLine) != 0) {
        if (!_res.hasCapturedLine) {
            strlcpy(_res.capturedLine, line, sizeof(_res.capturedLine));
            _res.hasCapturedLine = true;
        }
        finish_(true, false, false);
        return;
    }
}

AtSession::Result AtSession::takeResult() {
    if (_state != State::Done) return Result{};
    Result r = _res;
    _res = Result{};
    _state = State::Idle;
    return r;
}
