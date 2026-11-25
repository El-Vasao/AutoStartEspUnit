#pragma once

#include <stdint.h>
#include "common/Constants.h"

/**
 * POD-снимок состояния runtime для транспортов (Web/MQTT и т.п.).
 *
 * Цели:
 * - фиксированный размер, без кучи, можно копировать
 * - не тянуть крупные заголовки модулей в код транспорта
 */

struct StatusSnapshot {
    /// Последовательность состояния MQTT (1..; с переполнением). SSE её не использует.
    uint32_t seq{0};

    uint32_t uptimeSec{0};
    char modeName[16]{0};

    bool voltageValid{false};
    float voltage{0.0f};
    bool engineRunning{false};

    struct TempSensor {
        uint8_t id{0};         // номер шёлка T1…; 0 = неизвестен / не сопоставлен
        bool valid{false};
        uint32_t lastMs{0};
        float t{0.0f};
    };
    TempSensor tempSensors[HardwareLimits::SENSORS]{};

    bool relayState[HardwareLimits::RELAYS]{};
    bool inputState[HardwareLimits::INPUTS]{};

    bool programRunning{false};
    uint8_t currentProgramId{0};
    uint8_t lastProgramId{0};
    uint32_t timerRemainingSec{0};

    bool inputTriggerRuntime[Limits::MAX_TRIGGERS]{};
    bool tempTriggerRuntime[Limits::MAX_TRIGGERS]{};
    bool thermostatRuntime{false};
    bool thermostatAllowed{false};
    bool batterySaverRuntime{false};
    bool batterySaverAllowed{false};

    bool timeSynced{false};
    bool timeStale{false};
    uint32_t epochUtc{0};
    int16_t tzOffsetHours{0};
    char timeSource[12]{0};

    uint32_t freeHeap{0};

    /// Очередь недоставленных одноразовых ошибок MQTT (сначала новые). Пусто → на проводе нет `diag.last_err`.
    struct ErrEntry {
        uint8_t code{0};
        bool active{false};
        char msg[ErrorHistory::MSG_MAX]{};
        uint32_t uptimeSec{0};
    };
    ErrEntry lastErr[ErrorHistory::CAPACITY]{};
    uint8_t lastErrCount{0};

    /// Последний AT+CSQ: rssi 0..31 (99 — неизвестно), ber 0..7 (-1, если ещё не было).
    int16_t csqRssi{0};
    int16_t csqBer{-1};
};
