// include/mqtt/MqttClient.h
#pragma once

#include <Arduino.h>
#include <Client.h>

#include "app/StatusSnapshot.h"
#include "common/Constants.h"
#include "core/ErrorManager.h"
#include "mqtt/MqttCommand.h"

struct AppPorts;
#include "mqtt/MqttFsmClient.h"

/**
 * MQTT: online/state + programs/hwMap/trigMap + cmd/reply.
 * На линии: ack на входе (202|4xx|503), затем один тонкий итоговый ответ.
 * Слоты retain каталога — хеши из 8 hex; list/hwmap публикуют тела с retain=false.
 * run/stop/set/set_time: кольцо id (16) резервируется на входе.
 */
class MqttClient {
    friend struct MqttClientTestAccess;

public:
    explicit MqttClient(Client& client);

    void begin();
    void setAppPorts(const AppPorts* ports) { _ports = ports; }
    void setCtrlPlaneBusy(bool (*fn)(void*), void* ctx) {
        _ctrlPlaneBusy = fn;
        _ctrlPlaneBusyCtx = ctx;
    }
    void setTransportSendBusy(bool (*fn)(void*), void* ctx) {
        _transportSendBusy = fn;
        _transportSendBusyCtx = ctx;
    }
    void setOnRxIncomplete(void (*fn)(void*, const uint8_t*, uint16_t, uint32_t), void* ctx) {
        _onRxIncomplete = fn;
        _onRxIncompleteCtx = ctx;
    }

    void loop();
    void setReconnectEnabled(bool enabled) { _reconnectEnabled = enabled; }
    void onTransportRxOverflow();
    bool publishStatus(bool forceFull = false);
    void disconnect();
    /// Сброс сессии без online=false/DISCONNECT (модем soft-reboot / CFUN).
    void abortSessionHard(const char* reason);
    bool needsDisconnectDrain() const;
    uint8_t getConsecutiveConnectFails() const;
    bool isSessionConnected() const { return _fsm.isConnected(); }
    const char* getLastConnectFailReason() const;

    void onProgramLifecycle(uint8_t programId, bool finishedOk);
    static void onProgramLifecycleThunk(void* ctx, uint8_t programId, bool finishedOk);

#if defined(AUTOSTART_MQTT_DEBUG)
    /// Publish one downlink chunk on `{prefix}/cfg` or `{prefix}/prg` (suffix = "cfg"|"prg").
    bool publishDebugChunk(const char* suffix, const char* payload, uint16_t len);
    /// Final thin /reply after deferred cfg/prg flash commit.
    void publishDebugFinalReply(const char* id, uint16_t code);
#endif

private:
    enum class PendingKind : uint8_t { None, Thin, Status, List, HwMap, TrigMap };

    enum class BirthPhase : uint8_t {
        WaitSuback,
        CatalogPrograms,
        CatalogHwMap,
        CatalogTrigMap,
        FullState,
        OnlineTrue,
        Steady,
    };

    struct PendingReply {
        PendingKind kind{PendingKind::None};
        char id[MqttCmd::ID_MAX_LEN + 1]{};
        uint16_t code{0};
        uint32_t statusSeq{0}; ///< для тонкого ответа Status
    };

    struct IdRingEntry {
        char id[MqttCmd::ID_MAX_LEN + 1]{};
        uint16_t code{0};
        bool inFlight{false};
        bool used{false};
    };

    Client& _netClient;
    uint32_t _lastStatusPublish;
    uint32_t _lastEdgeProbe{0};
    uint32_t _lastReconnectAttempt;
    bool _reconnectEnabled{true};
    uint8_t _connectFailStreak{0};
    bool _sessionErrorRecorded{false};
    const AppPorts* _ports{nullptr};
    bool (*_ctrlPlaneBusy)(void*){nullptr};
    void* _ctrlPlaneBusyCtx{nullptr};
    bool (*_transportSendBusy)(void*){nullptr};
    void* _transportSendBusyCtx{nullptr};
    void (*_onRxIncomplete)(void*, const uint8_t*, uint16_t, uint32_t){nullptr};
    void* _onRxIncompleteCtx{nullptr};

    MqttFsmClient _fsm;
    bool _subscribeQueued{false};
    bool _mqttWasConnected{false};
    bool _subackTimeoutLogged{false};
    uint32_t _subWaitStartMs{0};

    BirthPhase _birth{BirthPhase::WaitSuback};
    bool _prefixTombstoneDue{false};
    char _bornPrefix[TextBytes::Mqtt::TOPIC_PREFIX]{};

    uint32_t _statusSeq{0}; ///< последний выданный seq (в очереди или уже доставлен)
    bool _haveSeq{false};
    uint32_t _deliveredSeq{0}; ///< последний Tele state, вышедший из очереди TX
    bool _haveDeliveredSeq{false};
    uint32_t _queuedStateSeq{0}; ///< seq Tele state, который сейчас в очереди FSM (0 = нет)
    bool _haveQueuedStateSeq{false};
    bool _queuedStateIsFull{false};

    StatusSnapshot _lastPublished{}; ///< база = последний снимок, кодированный к SEND OK
    StatusSnapshot _queuedEncoded{}; ///< снимок, кодированный в Tele из очереди (база при снятии)
    bool _haveQueuedEncoded{false};
    bool _havePublishedBaseline{false};

    uint32_t _publishedProgramsRev{0};
    uint32_t _publishedHwMapRev{0};
    uint32_t _publishedTrigMapRev{0};
    bool _programsCatalogDirty{true};
    bool _hwCatalogDirty{true};
    bool _trigCatalogDirty{true};

    bool _lastErrAwaitConfirm{false};
    ErrorSnapshotEntry _lastErrPending[ErrorHistory::CAPACITY]{};
    uint8_t _lastErrPendingCount{0};

    bool _cmdBusy{false};
    PendingReply _pending[MqttCmd::PENDING_REPLY_DEPTH]{};
    uint8_t _pendingCount{0};

    MqttCommand _inbound[MqttCmd::INBOUND_DEPTH]{};
    uint8_t _inboundHead{0};
    uint8_t _inboundCount{0};

    char _activeRunId[MqttCmd::ID_MAX_LEN + 1]{};
    uint8_t _activeRunProgram{0};
    bool _hasActiveRun{false};
    // Прогон, который start()→stop() уже прервал; жизненный цикл ещё не пришёл.
    char _preemptedRunId[MqttCmd::ID_MAX_LEN + 1]{};
    uint8_t _preemptedRunProgram{0};
    bool _hasPreemptedRun{false};

    IdRingEntry _idRing[MqttCmd::ID_RING_DEPTH]{};
    uint8_t _idRingNext{0};

    char _mqttClientId[TextBytes::Mqtt::CLIENT_ID]{};
    char _mqttUser[TextBytes::Mqtt::USER]{};
    char _mqttPass[TextBytes::Mqtt::PASS]{};
    char _topicOnline[TextBytes::Mqtt::TOPIC]{};
    char _topicState[TextBytes::Mqtt::TOPIC]{};
    char _topicPrograms[TextBytes::Mqtt::TOPIC]{};
    char _topicHwMap[TextBytes::Mqtt::TOPIC]{};
    char _topicTrigMap[TextBytes::Mqtt::TOPIC]{};
    char _topicCmd[TextBytes::Mqtt::TOPIC]{};
    char _topicReply[TextBytes::Mqtt::TOPIC]{};
#if defined(AUTOSTART_MQTT_DEBUG)
    char _topicCfg[TextBytes::Mqtt::TOPIC]{};
    char _topicPrg[TextBytes::Mqtt::TOPIC]{};
#endif

    void connect();
    void noteSessionErrorIfNeeded_();
    static void joinTopic_(char* out, size_t outSz, const char* prefix, const char* suffix);
    void rebuildTopics_(const char* prefix);

    static void onPublishThunk(void* ctx, const char* topic, const uint8_t* payload, uint16_t len, bool retained);
    static void onTeleDequeuedThunk(void* ctx, MqttFsmClient::TeleKind kind);
    void onTeleDequeued_(MqttFsmClient::TeleKind kind);
    void handlePublish(const char* topic, const uint8_t* payload, uint16_t len, bool retained);
    void tryAckIngress_(const uint8_t* payload, uint16_t length);
    void dispatchCommand_(const MqttCommand& cmd);

    void clearPending_();
    void clearInbound_();
    void clearIdRing_();
    void resetSessionLocal_();
    void failQueuedInboundOnDisconnect_();

    bool enqueueInbound_(const MqttCommand& cmd);
    void drainInbound_();

    bool enqueuePending_(const PendingReply& pr);
    void flushPending_();
    bool stageReply_(const PendingReply& pr);
    bool ctrlPlaneBlocked_() const;
    bool publishThinReply_(const char* id, uint16_t code, uint32_t statusSeq = 0);
    bool tryPublishEdgeStatus_();
    /// `fullErrHistory`: full/birth/status — кольцо истории; иначе только undelivered (edge).
    void captureStatusSnapshot_(StatusSnapshot& out, bool fullErrHistory) const;

    bool publishProgramsHash_();
    bool publishHwMapHash_();
    bool publishTrigMapHash_();
    bool publishProgramsBody_();
    bool publishHwMapBody_();
    bool publishTrigMapBody_();
    bool publishOnline_(bool on);
    bool publishEmptyRetain_(const char* topic);
    void tickBirth_(bool txOk, bool subReady, bool teleIdle);
    void tickSteady_(bool txOk, bool subReady, bool teleIdle);

    bool validateArgs_(const MqttCommand& cmd, MqttCmdErr& errOut) const;
    void confirmLastErrDelivery_();
    void abandonLastErrDelivery_();

    /// Поиск в кольце id. Возвращает запись или nullptr.
    IdRingEntry* findIdRing_(const char* id);
    /// Резервирует id как inFlight. false, если уже известен или каждый слот всё ещё inFlight (noSlotOut).
    bool reserveIdRing_(const char* id, uint16_t* existingCodeOut, bool* existingInFlightOut, bool* noSlotOut);
    void releaseIdRing_(const char* id);
    void finalizeIdRing_(const char* id, uint16_t code);
    void replyRunFinal_(const char* id, bool finishedOk);
};
