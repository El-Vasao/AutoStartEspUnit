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

    char lastError[64]{0};
};

