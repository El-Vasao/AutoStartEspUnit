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
 * MQTT: status/avail + cmd/reply.
 * Wire: ingress ack (202|4xx|503) then one final (thin id+code; list/status fat body).
 * `id` correlates request/reply only — no idempotency LRU.
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
    /// True while modem CIPSEND/TX owns the bus — gate Ctrl replies (ack then final).
    void setCtrlPlaneBusy(bool (*fn)(void*), void* ctx) {
        _ctrlPlaneBusy = fn;
        _ctrlPlaneBusyCtx = ctx;
    }

    void loop();
    void setReconnectEnabled(bool enabled) { _reconnectEnabled = enabled; }
    /// Transport RX ring overflow → force MQTT error/reconnect (stream desync).
    void onTransportRxOverflow();
    bool publishStatus(bool forceFull = false);
    void disconnect();
    bool needsDisconnectDrain() const;
    uint8_t getConsecutiveConnectFails() const;
    /// Last MQTT session fail reason (`keepalive_timeout` / `tcp_drop` / …); "" if none.
    const char* getLastConnectFailReason() const;
    bool isNonBlocking() const { return true; }

    void onProgramLifecycle(uint8_t programId, bool finishedOk);
    static void onProgramLifecycleThunk(void* ctx, uint8_t programId, bool finishedOk);

private:
    enum class PendingKind : uint8_t { None, Thin, List, Status };

    struct PendingReply {
        PendingKind kind{PendingKind::None};
        char id[MqttCmd::ID_MAX_LEN + 1]{};
        uint16_t code{0};
    };

    Client& _netClient;
    uint32_t _lastStatusPublish;
    uint32_t _lastReconnectAttempt;
    bool _reconnectEnabled{true};
    uint8_t _connectFailStreak{0};
    const AppPorts* _ports{nullptr};
    bool (*_deferMqttRx)(void*){nullptr};
    void* _deferMqttRxCtx{nullptr};
    bool (*_ctrlPlaneBusy)(void*){nullptr};
    void* _ctrlPlaneBusyCtx{nullptr};

    MqttFsmClient _fsm;
    bool _subscribeQueued{false};
    bool _mqttWasConnected{false};
    bool _onlinePublishDue{false};
    bool _awaitFirstStatus{false};
    bool _subackTimeoutLogged{false};
    uint32_t _firstStatusAfterMs{0};
    uint32_t _subWaitStartMs{0};
    StatusSnapshot _lastPublished{};
    bool _havePublishedBaseline{false};

    bool _cmdBusy{false};
    PendingReply _pending[MqttCmd::PENDING_REPLY_DEPTH]{};
    uint8_t _pendingCount{0};

    MqttCommand _inbound[MqttCmd::INBOUND_DEPTH]{};
    uint8_t _inboundHead{0};
    uint8_t _inboundCount{0};

    char _activeRunId[MqttCmd::ID_MAX_LEN + 1]{};
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
    /// Ingress only: parse/validate/queue → ack 202 or reject; never runs handlers.
    void tryAckIngress_(const uint8_t* payload, uint16_t length);
    void dispatchCommand_(const MqttCommand& cmd);

    void clearPending_();
    void clearInbound_();

    bool enqueueInbound_(const MqttCommand& cmd);
    void drainInbound_();

    bool enqueuePending_(const PendingReply& pr);
    void evictOldestPending_();
    void flushPending_();
    bool stageReply_(const PendingReply& pr);
    bool ctrlPlaneBlocked_() const;
    bool publishThinReply_(const char* id, uint16_t code);
    bool publishListReply_(const char* id);
    bool publishStatusReply_(const char* id);
    void captureStatusSnapshot_(StatusSnapshot& out) const;
    bool validateArgs_(const MqttCommand& cmd, MqttCmdErr& errOut) const;
};
