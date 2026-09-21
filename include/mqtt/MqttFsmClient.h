#pragma once

#include <Arduino.h>
#include <Client.h>

// Minimal non-blocking MQTT client (FSM) for embedded constraints.
// Goals:
// - no dynamic allocation
// - strict per-tick budgets (caller controls read/write pumping)
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

    // Subscribe QoS0. Allowed in Connected state; queued otherwise.
    bool subscribe(const char* topic);

    // Publish QoS0. Returns false if cannot queue now.
    bool publish(const char* topic, const uint8_t* payload, uint16_t len, bool retained);

    /** Publish QoS0 JSON: byte-length preflight via `measure(Print&)` then serialize into TX buffer. */
    using JsonPrintEncodeFn = size_t (*)(Print& p, void* ctx);
    bool publishPrintedMeasured(const char* topic, JsonPrintEncodeFn encoder, void* ctx, uint16_t maxPayloadBytes,
                                bool retained, size_t* measuredBytesOut = nullptr);

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

    // Pending control actions
    bool _connectRequested{false};
    bool _disconnectRequested{false};
    bool _subRequested{false};
    bool _awaitingSuback{false};

    // Single in-flight packet id (for SUBSCRIBE / SUBACK).
    uint16_t _nextPacketId{1};
    uint16_t _subPacketId{0};

    // Queued subscribe topic (single)
    static constexpr uint8_t TOPIC_MAX = 56;
    char _subTopic[TOPIC_MAX]{};

    // TX staging buffer (single packet at a time).
    // Wire: 1 (fixed hdr) + up to 4 (remaining length) + 2 (topic len) + topicLen + payloadLen <= TX_MAX.
    static constexpr uint16_t TX_MAX = 576;
    uint8_t _tx[TX_MAX]{};
    uint16_t _txLen{0};
    uint16_t _txOff{0};

    // RX frame assembly (header + remaining length + payload). Must cover worst-case cmd PUBLISH (topic + CMD_JSON_MAX).
    static constexpr uint16_t RX_MAX = 320;
    uint8_t _rx[RX_MAX]{};
    uint16_t _rxLen{0};

    // Helpers
    void resetSession_();
    void setError_(const char* reason);

    bool ensureTcp_();
    void maybeSendPing_(uint32_t now);
    void pumpWrite_(uint16_t maxBytes);
    void pumpReadAndParse_(uint16_t maxBytes, uint16_t maxFrames);

    // Packet builders (write into _tx)
    bool buildConnect_();
    bool buildDisconnect_();
    bool buildPingreq_();
    bool buildSubscribe_(const char* topic);
    bool buildPublish_(const char* topic, const uint8_t* payload, uint16_t len, bool retained);

    // Parsers
    bool parseOneFrame_();

    // Encoding utils
    static uint16_t writeU16_(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)(v & 0xFF); return 2; }
    static uint16_t putStr_(uint8_t* p, uint16_t cap, const char* s);
    static uint8_t encodeRemainingLen_(uint32_t len, uint8_t out[4]);
    static bool decodeRemainingLen_(const uint8_t* p, uint16_t cap, uint32_t& outLen, uint8_t& outUsed);
};

