#pragma once

#include <stdint.h>
#include "common/Constants.h"

/**
 * POD snapshot of runtime state for transports (Web/MQTT/etc).
 *
 * Goals:
 * - fixed-size, no heap, safe to copy
 * - avoid pulling large module headers into transport code
 */

struct StatusSnapshot {
    uint32_t uptimeSec{0};
    char modeName[16]{0};

    bool voltageValid{false};
    float voltage{0.0f};
    bool engineRunning{false};

    struct TempSensor {
        uint16_t id{0};        // 0 when unknown/unmapped
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

    bool inputTriggerRuntime[Limits::MAX_TRIGGERS]{};
    bool tempTriggerRuntime[Limits::MAX_TRIGGERS]{};

    bool timeSynced{false};
    bool timeStale{false};
    uint32_t epochUtc{0};
    int16_t tzOffsetHours{0};
    char timeSource[12]{0};

    /// Undelivered MQTT error one-shot queue (newest first). Empty → omit `last_err` on wire.
    struct ErrEntry {
        uint8_t code{0};
        bool active{false};
        char msg[ErrorHistory::MSG_MAX]{};
        uint32_t uptimeSec{0};
    };
    ErrEntry lastErr[ErrorHistory::CAPACITY]{};
    uint8_t lastErrCount{0};

    /// Last AT+CSQ: rssi 0..31 (99 unknown), ber 0..7 (-1 if never seen).
    int16_t csqRssi{0};
    int16_t csqBer{-1};
};
