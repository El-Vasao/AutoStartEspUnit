#pragma once

#include <Arduino.h>
#include <stdint.h>

class RelayController;
class DigitalInputs;
class SensorsController;
class ProgramExecutor;
class GSMController;
class ErrorManager;
class FlashCommitCoordinator;
class MQTTClient;

/**
 * Narrow read-side port for SSE/bootstrap JSON builders.
 * Filled once per tick/snapshot from composition root; builders must not include Core.h.
 */
struct SseStatusPort {
    RelayController* relay{nullptr};
    DigitalInputs* inputs{nullptr};
    SensorsController* sensors{nullptr};
    ProgramExecutor* program{nullptr};
    GSMController* gsm{nullptr};
    MQTTClient* mqtt{nullptr};
    ErrorManager* errors{nullptr};
    FlashCommitCoordinator* flash{nullptr};

    const char* modeName{""};
    uint32_t uptimeSec{0};
    uint32_t freeHeap{0};
    bool engineRunning{false};
    bool thermostatRuntime{false};
    bool batterySaverRuntime{false};

    bool timeSynced{false};
    bool timeStale{false};
    uint32_t epochUtc{0};
    int16_t tzOffsetHours{0};
    char timeSource[12]{0};

    /// GSM CSQ + MQTT session (compact SoftAP debug parity with MQTT /status).
    int16_t csqRssi{99};
    int16_t csqBer{-1};
    bool mqttConnected{false};

    bool (*getTriggerRuntime)(void* ctx, uint8_t index){nullptr};
    bool (*getTempTriggerRuntime)(void* ctx, uint8_t index){nullptr};
    void* triggerCtx{nullptr};
};
