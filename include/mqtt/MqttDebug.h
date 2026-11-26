#pragma once

#if defined(AUTOSTART_MQTT_DEBUG)

#include <stddef.h>
#include <stdint.h>

#include "common/Constants.h"
#include "mqtt/MqttCommand.h"

class MqttClient;
class SensorsController;

/// Upload session + path set / get / reset for debug MQTT (gated by AUTOSTART_MQTT_DEBUG).
class MqttDebugSession {
public:
    bool busy() const { return state_ != State::Idle; }

    /// True while commit is deferred to FlashCommitCoordinator (final /reply not yet staged).
    bool awaitingFlashCommit() const { return state_ == State::CommitPending; }

    /// Hold base64 from ingress JSON until `put` runs in dispatch (inbound queue has no payload).
    bool holdPutB64(const char* b64);

    /// Returns HTTP-like final code (200/4xx/5xx). Does not stage replies.
    /// Returns 0 when commit was deferred (final reply later via tickFlashCommit).
    uint16_t handle(const MqttCommand& cmd, MqttClient& mqtt, SensorsController& sensors);

    void abort();

    /// Drain pending downlink chunks (call from MqttClient::loop while connected).
    void tickDownlink(MqttClient& mqtt);

    /// When CommitPending and flashCommit finished, stage final /reply (200/422).
    void tickFlashCommit(MqttClient& mqtt);

private:
    enum class State : uint8_t { Idle, Receiving, Sending, CommitPending };

    enum class Target : uint8_t { None, Cfg, Prg };

    State state_{State::Idle};
    Target target_{Target::None};
    uint32_t expectBytes_{0};
    uint32_t gotBytes_{0};
    uint32_t expectCrc_{0};
    uint16_t expectN_{0};
    uint16_t nextI_{0};
    uint8_t programId_{0};
    uint8_t buf_[MqttDebugLimits::PUT_MAX]{};
    char holdB64_[MqttDebugLimits::CHUNK_B64_MAX + 1]{};
    char pendingReplyId_[MqttCmd::ID_MAX_LEN + 1]{};

    // Downlink
    uint16_t sendI_{0};
    uint16_t sendN_{0};
    uint32_t sendCrc_{0};
    uint32_t sendLen_{0};
    uint32_t sendOff_{0};
    uint32_t lastSendMs_{0};
    char sendTopicSuffix_[8]{};

    uint16_t begin_(const MqttCommand& cmd, Target t);
    uint16_t put_(const MqttCommand& cmd);
    uint16_t commit_(SensorsController& sensors);
    uint16_t doGet_(const MqttCommand& cmd, MqttClient& mqtt);
    uint16_t doSet_(const MqttCommand& cmd);
    uint16_t doReset_(const MqttCommand& cmd);
    uint16_t doDelete_(const MqttCommand& cmd);
    void clearUploadFields_();
};

uint32_t mqttDebugCrc32(const uint8_t* data, size_t len);
bool mqttDebugHexToU32(const char* hex8, uint32_t* out);
void mqttDebugU32ToHex(uint32_t v, char out[9]);
size_t mqttDebugBase64Decode(const char* b64, uint8_t* out, size_t outCap);
size_t mqttDebugBase64Encode(const uint8_t* in, size_t inLen, char* out, size_t outCap);

extern MqttDebugSession mqttDebugSession;

#endif // AUTOSTART_MQTT_DEBUG
