#pragma once

#include <Arduino.h>
#include <Client.h>

#include "common/Constants.h"

// Minimal non-blocking MQTT client (FSM) for embedded constraints.
// Goals:
// - no dynamic allocation
// - strict per-tick budgets (caller controls read/write pumping)
// - outbound packet queue with Ctrl vs Tele priority (reserve + status coalesce)
// - supports: CONNECT (3.1): will QoS 0..2, retained will; CONNACK, SUBSCRIBE QoS0, SUBACK, PUBLISH QoS0, PINGREQ/PINGRESP, DISCONNECT
class MqttFsmClient {
public:
    enum class Proto : uint8_t { Mqtt31, Mqtt311 };

    enum class State : uint8_t {
        Idle,
        TcpConnecting,
        MqttConnecting,
        Connected,
        Error,
    };

    /// Outbound priority: control (session/presence/replies) vs telemetry (status).
    enum class OutClass : uint8_t { Ctrl, Tele };

    struct Config {
        const char* host{nullptr};
        uint16_t port{1883};

        Proto proto{Proto::Mqtt311};
        const char* clientId{nullptr};
        const char* username{nullptr};
        const char* password{nullptr};

        const char* willTopic{nullptr};
        const char* willMsg{nullptr};
        bool willRetain{true};
        /// MQTT Will QoS (0..2). CONNECT only; publishes from this stack remain QoS0.
        uint8_t willQos{1};

        uint16_t keepAliveSec{30};
        bool cleanSession{true};
    };

    struct Budgets {
        uint16_t maxReadBytesPerTick{128};
        uint16_t maxWriteBytesPerTick{128};
        uint16_t maxParseFramesPerTick{4};
        /// Wall-clock cap for write+read pumps in one tick (Interrupt WDT safety).
        uint16_t maxMsPerTick{20};
        /// Optional: after pumpWrite, skip RX if transport TX/CIPSEND still owns the bus.
        bool (*shouldDeferRead)(void* ctx){nullptr};
        void* shouldDeferReadCtx{nullptr};
    };

    // Callback for incoming publish QoS0.
    using PublishCb = void (*)(void* ctx, const char* topic, const uint8_t* payload, uint16_t len, bool retained);

    explicit MqttFsmClient(Client& netClient);

    void setPublishCallback(PublishCb cb, void* ctx) { _pubCb = cb; _pubCbCtx = ctx; }

    void begin(const Config& cfg);
    void requestConnect();
    void requestDisconnect();

    // One non-blocking tick: progresses TCP connect, sends pending control packets, reads & parses RX.
    void tick(const Budgets& b);

    State state() const { return _state; }
    bool isConnected() const { return _state == State::Connected; }
    bool isDisconnectPending() const { return _disconnectRequested; }
    /// True until SUBSCRIBE is built and SUBACK observed (or no subscribe was requested).
    bool isSubscribePending() const { return _subRequested || _awaitingSuback; }
    /// True after SUBACK with granted QoS (not 0x80). Cleared on session reset.
    bool isSubscribeConfirmed() const { return _subscribeConfirmed; }
    /// True while any outbound MQTT packet is queued or mid-write.
    bool hasOutbound() const { return _txQCount != 0; }
    /// True if any Ctrl (reply/PING/avail/…) is queued or mid-send.
    bool hasCtrlOutbound() const;

    /// Force Error state (e.g. transport RX overflow) → reconnect path.
    void forceError(const char* reason);
    /// Last `setError_` / `forceError` reason (stable c-string; "" if none).
    const char* lastErrorReason() const { return _lastErrorReason; }

    // Subscribe QoS0. Allowed in Connected state; queued otherwise.
    bool subscribe(const char* topic);
    /// Drop in-flight SUBACK wait and re-queue SUBSCRIBE (same topic).
    void requeueSubscribe();

    // Publish QoS0. `control=true` uses Ctrl class (may fill last free slot).
    // `control=false` is Tele: requires reserve or coalesces pending status.
    bool publish(const char* topic, const uint8_t* payload, uint16_t len, bool retained, bool control = true);

    /** Publish QoS0 JSON into a queue slot (or coalesce Tele). */
    using JsonPrintEncodeFn = size_t (*)(Print& p, void* ctx);
    bool publishPrintedMeasured(const char* topic, JsonPrintEncodeFn encoder, void* ctx, uint16_t maxPayloadBytes,
                                bool retained, bool control = false, size_t* measuredBytesOut = nullptr);

    // Millis of last rx activity (any valid MQTT frame).
    uint32_t lastRxMs() const { return _lastRxMs; }

private:
    Client& _net;
    Config _cfg{};
    Budgets _budgets{};
    State _state{State::Idle};

    PublishCb _pubCb{nullptr};
    void* _pubCbCtx{nullptr};

    // Timers
    uint32_t _tcpConnectStartMs{0};
    uint32_t _mqttHandshakeStartMs{0};
    uint32_t _lastRxMs{0};
    uint32_t _lastTxMs{0};
    uint32_t _lastPingMs{0};
    const char* _lastErrorReason{""};

    // Pending control actions
    bool _connectRequested{false};
    bool _disconnectRequested{false};
    bool _subRequested{false};
    bool _awaitingSuback{false};
    bool _subscribeConfirmed{false};

    // Single in-flight packet id (for SUBSCRIBE / SUBACK).
    uint16_t _nextPacketId{1};
    uint16_t _subPacketId{0};

    // Queued subscribe topic (single). Match TextBytes::Mqtt::TOPIC so long prefixes work.
    static constexpr size_t TOPIC_MAX = TextBytes::Mqtt::TOPIC;
    char _subTopic[TOPIC_MAX]{};

    // Outbound packet queue: wire-ready MQTT frames.
    // Depth 3: Ctrl reserve + one Tele (coalesced) + one in-flight Ctrl (SUBSCRIBE/online/PING).
    static constexpr uint16_t TX_MAX = 1024;
    static constexpr uint8_t TX_Q_DEPTH = 3;
    uint8_t _txQ[TX_Q_DEPTH][TX_MAX]{};
    uint16_t _txQLen[TX_Q_DEPTH]{};
    OutClass _txQClass[TX_Q_DEPTH]{};
    uint8_t _txQHead{0};  ///< next packet to send
    uint8_t _txQTail{0};  ///< next free slot
    uint8_t _txQCount{0};
    uint16_t _txOff{0};   ///< byte offset into head packet while writing

    // RX frame assembly
    static constexpr uint16_t RX_MAX = 320;
    static constexpr uint32_t RX_ASSEMBLE_TIMEOUT_MS = 3000;
    uint8_t _rx[RX_MAX]{};
    uint16_t _rxLen{0};
    uint32_t _rxAssembleStartMs{0};

    void resetSession_();
    void setError_(const char* reason);
    void clearTxQueue_();
    bool queueHasRoom_() const { return _txQCount < TX_Q_DEPTH; }
    uint8_t freeSlots_() const { return (uint8_t)(TX_Q_DEPTH - _txQCount); }
    bool hasCtrlInQueue_() const;
    /// Index of replaceable Tele slot, or -1. Skips mid-send head.
    int findReplaceableTele_() const;
    bool beginQueueSlot_(OutClass cls, uint8_t*& buf, uint16_t& cap);
    void commitQueueSlot_(uint16_t len, OutClass cls);
    /// Overwrite an existing Tele slot (coalesce).
    bool beginReplaceTeleSlot_(uint8_t*& buf, uint16_t& cap, uint8_t& slotIdxOut);
    void commitReplaceTeleSlot_(uint8_t slotIdx, uint16_t len);

    bool ensureTcp_();
    void maybeSendPing_(uint32_t now);
    /// Connected dead-man: no MQTT RX for 1.5× keepAlive → Error.
    void maybeKeepaliveWatchdog_(uint32_t now);
    void pumpWrite_(uint16_t maxBytes, uint32_t deadlineMs);
    void pumpReadAndParse_(uint16_t maxBytes, uint16_t maxFrames, uint32_t deadlineMs);
    /// Drop one RX head byte while hunting MQTT frame sync (no session tear-down).
    bool resyncDropOne_(const char* why);
    /// True if head is a valid MQTT type+remLen and buffer is still short of totalLen.
    bool stallIsValidIncomplete_(uint32_t& needOut) const;
    static bool pastDeadline_(uint32_t deadlineMs);

    bool buildConnect_();
    bool buildDisconnect_();
    bool buildPingreq_();
    bool buildSubscribe_(const char* topic);
    bool buildPublish_(const char* topic, const uint8_t* payload, uint16_t len, bool retained, OutClass cls);

    bool parseOneFrame_();

    static uint16_t writeU16_(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)(v & 0xFF); return 2; }
    static uint16_t putStr_(uint8_t* p, uint16_t cap, const char* s);
    static uint8_t encodeRemainingLen_(uint32_t len, uint8_t out[4]);
    static bool decodeRemainingLen_(const uint8_t* p, uint16_t cap, uint32_t& outLen, uint8_t& outUsed);
};
