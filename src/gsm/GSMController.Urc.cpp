/**
 * @file GSMController.Urc.cpp
 * @brief URC/await слой GSMController: парсинг строк модема, ожидание OK/ERROR/+CREG/IP.
 *
 * Назначение:
 * - Разделить URC-first обработку входящих строк (URC) и “ожидания” (await) от FSM.
 * - Дать FSM простые флаги `_awaitOk/_awaitError/_awaitGotIp/...` без тяжёлых строковых операций.
 *
 * Память:
 * - Только фиксированные буферы и простые парсеры (`strtol`, `strstr`), без `String`.
 *
 * Запрещено:
 * - Добавлять сюда зависимость от web/mqtt и т.п. Это чисто GSM слой.
 */
#include "gsm/GSMController.h"

#include "gsm/GsmAtParse.h"
#include "core/Core.h"
#include "common/Logger.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

void GSMController::resetAwait() {
    _await = AwaitKind::NONE;
    _awaitDeadlineMs = 0;
    _awaitOk = false;
    _awaitError = false;
    _awaitGotCreg = false;
    _awaitGotIp = false;
    _awaitCmdId = 0;
    _awaitLinesLeft = 0;
}

void GSMController::beginAwait(AwaitKind kind, uint32_t timeoutMs) {
    resetAwait();
    _await = kind;
    _awaitDeadlineMs = millis() + timeoutMs;
    _awaitCmdId = _cmdId;
    _awaitLinesLeft = 8;
}

bool GSMController::awaitTimedOut(uint32_t now) const {
    if (_await == AwaitKind::NONE) return false;
    return (int32_t)(now - _awaitDeadlineMs) >= 0;
}

static bool parseCopsQuotedOperator_(const char* line, char* out, size_t outCap) {
    if (!line || !out || outCap == 0) return false;
    const char* p = strstr(line, "+COPS:");
    if (!p) return false;
    const char* q = strchr(p, '"');
    if (!q) return false;
    q++;
    const char* r = strchr(q, '"');
    if (!r || r <= q) return false;
    size_t n = (size_t)(r - q);
    if (n >= outCap) n = outCap - 1;
    memcpy(out, q, n);
    out[n] = '\0';
    return out[0] != '\0';
}

void GSMController::onRxLine(const char* line) {
    if (!line || !line[0]) return;
    handleUrc(line);
    handleAwaitLine(line);
}

void GSMController::handleUrc(const char* line) {
    if (!line || !line[0]) return;
    if (strncmp(line, "+CSQ:", 5) == 0) {
        int rssi = -1;
        int ber = -1;
        if (sscanf(line, "+CSQ: %d,%d", &rssi, &ber) == 2) {
            _signalBer = (int16_t)ber;
            if (rssi >= 0 && rssi <= 31) {
                const int16_t prev = _signal;
                const int16_t delta =
                    (prev >= 0 && prev <= 31)
                        ? (int16_t)((rssi > prev) ? (rssi - prev) : (prev - rssi))
                        : GSM::URC_RSSI_LOG_DELTA;
                if (delta >= GSM::URC_RSSI_LOG_DELTA) {
                    const int dbm = -113 + 2 * rssi;
                    logger.log("[GSMController] CSQ: rssi=%d (%d dBm) ber=%d\n", rssi, dbm, ber);
                }
                _signal = (int16_t)rssi;
            } else if (rssi >= 0) {
                _signal = (int16_t)rssi;
            }
        }
        return;
    }
    if (strncmp(line, "+COPS:", 6) == 0) {
        char tmp[TextBytes::Gsm::OPERATOR];
        if (parseCopsQuotedOperator_(line, tmp, sizeof(tmp)) && strcmp(tmp, _operator) != 0) {
            strlcpy(_operator, tmp, sizeof(_operator));
            logger.log("[GSMController] operator: %s\n", _operator);
        }
        return;
    }
    if (strcmp(line, "RDY") == 0 || strcmp(line, "Call Ready") == 0 || strcmp(line, "SMS Ready") == 0) {
        _modemRestartedSeen = true;
        ev(10);
        return;
    }
    // NOTE: modem MQTT (+SM*) URCs forwarding was removed as we are moving to TCP URC-first transport.
    int8_t v = -1;
    if (gsm_at::parseCregStat(line, v)) {
        _cregStat = v;
        return;
    }
    if (gsm_at::parseCgattStat(line, v)) {
        _cgattStat = v;
        return;
    }
    if (strstr(line, "PDP DEACT") != nullptr) {
        _pdpDeactSeen = true;
        ev(11);
        return;
    }
    if (strcmp(line, "CLOSED") == 0) {
        _closedSeen = true;
        ev(12);
        return;
    }
}

static bool isWordError_(const char* line) {
    if (!line) return false;
    return (strcmp(line, "NO CARRIER") == 0) ||
           (strcmp(line, "NO ANSWER") == 0) ||
           (strcmp(line, "BUSY") == 0);
}

void GSMController::handleAwaitLine(const char* line) {
    if (_await == AwaitKind::NONE) return;
    if (_awaitCmdId != _cmdId) return;
    if (!line || !line[0]) return;

    if (_awaitLinesLeft > 0) _awaitLinesLeft--;

    if (strcmp(line, "OK") == 0) {
        _awaitOk = true;
        return;
    }

    if (strcmp(line, "ERROR") == 0 || strstr(line, "+CME ERROR") != nullptr || isWordError_(line)) {
        _awaitError = true;
        return;
    }

    if (_await == AwaitKind::CREG) {
        int8_t st = -1;
        if (gsm_at::parseCregStat(line, st)) {
            _cregStat = st;
            _awaitGotCreg = true;
            return;
        }
    }

    if (_await == AwaitKind::IP) {
        if (gsm_at::isIpv4Line(line) || gsm_at::sapbrLineHasQuotedIpv4(line)) {
            _awaitGotIp = true;
            return;
        }
    }
}
