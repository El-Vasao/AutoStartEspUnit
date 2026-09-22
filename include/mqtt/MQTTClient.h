// include/mqtt/MQTTClient.h
#pragma once

#include <Arduino.h>
#include <Client.h>

#include "app/StatusSnapshot.h"
#include "common/Constants.h"
#include "mqtt/MqttCommand.h"

struct AppPorts;
#include "mqtt/MqttFsmClient.h"

/**
 * MQTT: status/avail + cmd/reply contract (id, cmd, set/run/stop/list/status).
 */
class MQTTClient {
public:
    explicit MQTTClient(Client& client);

    void begin();
    void setAppPorts(const AppPorts* ports) { _ports = ports; }
    void setDeferMqttRx(bool (*fn)(void*), void* ctx) {
        _deferMqttRx = fn;
        _deferMqttRxCtx = ctx;
    }

    void loop();
    void setReconnectEnabled(bool enabled) { _reconnectEnabled = enabled; }
    bool publishStatus(bool forceFull = false);
    void disconnect();
    bool needsDisconnectDrain() const;
    uint8_t getConsecutiveConnectFails() const;
    bool isNonBlocking() const { return true; }

    /// ProgramExecutor lifecycle → /reply for active run id.
    void onProgramLifecycle(uint8_t programId, bool finishedOk);
    static void onProgramLifecycleThunk(void* ctx, uint8_t programId, bool finishedOk);

private:
    static constexpr uint8_t kIdemDepth = 16;
    static constexpr uint32_t kIdemTtlMs = 30UL * 60UL * 1000UL;
    static constexpr uint8_t kPendingReplyDepth = 2;

    struct IdemEntry {
        char id[17]{};
        uint32_t argsFp{0};
        MqttCommandKind kind{MqttCommandKind::None};
        bool ok{false};
        MqttCmdErr err{MqttCmdErr::None};
        MqttRunState runState{MqttRunState::None};
        uint32_t atMs{0};
        bool used{false};
    };

    enum class PendingKind : uint8_t { None, Thin, List, StatusAndAck };

    struct PendingReply {
        PendingKind kind{PendingKind::None};
        char id[17]{};
        MqttCommandKind cmd{MqttCommandKind::None};
        bool ok{false};
        MqttCmdErr err{MqttCmdErr::None};
        MqttRunState runState{MqttRunState::None};
    };

    Client& _netClient;
    uint32_t _lastStatusPublish;
    uint32_t _lastReconnectAttempt;
    bool _reconnectEnabled{true};
    uint8_t _connectFailStreak{0};
    const AppPorts* _ports{nullptr};
    bool (*_deferMqttRx)(void*){nullptr};
    void* _deferMqttRxCtx{nullptr};

    MqttFsmClient _fsm;
    bool _subscribed{false};
    bool _mqttWasConnected{false};
    bool _onlinePublishDue{false};
    bool _awaitFirstStatus{false};
    bool _subackTimeoutLogged{false};
    uint32_t _firstStatusAfterMs{0};
    uint32_t _subWaitStartMs{0};
    StatusSnapshot _lastPublished{};
    bool _havePublishedBaseline{false};

    bool _cmdBusy{false};
    IdemEntry _idem[kIdemDepth]{};
    uint8_t _idemNext{0};
    PendingReply _pending[kPendingReplyDepth]{};
    uint8_t _pendingCount{0};

    char _activeRunId[17]{};
    uint8_t _activeRunProgram{0};
    bool _hasActiveRun{false};

    char _mqttClientId[TextBytes::Mqtt::CLIENT_ID]{};
    char _mqttUser[TextBytes::Mqtt::USER]{};
    char _mqttPass[TextBytes::Mqtt::PASS]{};
    char _topicAvail[TextBytes::Mqtt::TOPIC]{};
    char _topicStatus[TextBytes::Mqtt::TOPIC]{};
    char _topicCmd[TextBytes::Mqtt::TOPIC]{};
    char _topicReply[TextBytes::Mqtt::TOPIC]{};

    void connect();
    static void joinTopic_(char* out, size_t outSz, const char* prefix, const char* suffix);

    static void onPublishThunk(void* ctx, const char* topic, const uint8_t* payload, uint16_t len, bool retained);
    void handlePublish(const char* topic, const uint8_t* payload, uint16_t len, bool retained);
    void dispatchCommand_(const MqttCommand& cmd);
    void clearIdem_();
    void clearPending_();
    uint32_t argsFingerprint_(const MqttCommand& cmd) const;
    IdemEntry* findIdem_(const char* id);
    void storeIdem_(const MqttCommand& cmd, bool ok, MqttCmdErr err, MqttRunState st);
    void updateIdemRunState_(const char* id, bool ok, MqttCmdErr err, MqttRunState st);

    bool enqueuePending_(const PendingReply& pr);
    void flushPending_();
    bool publishThinReply_(const char* id, MqttCommandKind cmd, bool ok, MqttCmdErr err, MqttRunState st);
    bool publishListReply_(const char* id);
    bool tryPublishThinOrQueue_(const char* id, MqttCommandKind cmd, bool ok, MqttCmdErr err, MqttRunState st);
    bool validateArgs_(const MqttCommand& cmd, MqttCmdErr& errOut) const;
};
