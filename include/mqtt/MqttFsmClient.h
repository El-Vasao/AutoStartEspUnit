#pragma once

#include <Arduino.h>
#include <Client.h>

#include "common/Constants.h"

// Минимальный неблокирующий MQTT-клиент (FSM) под ограничения встраиваемой системы.
// Цели:
// - без динамического выделения
// - жёсткие бюджеты на тик (сколько читать и писать, решает вызывающий)
// - очередь исходящих с приоритетом Ctrl над Tele (Full/Catalog не схлопываются; Delta схлопывается)
// - CONNECT will QoS0..2; SUBSCRIBE QoS1; PUBLISH QoS0 tele + QoS1 тонкий ответ; PUBACK
class MqttFsmClient {
public:
    enum class Proto : uint8_t { Mqtt311 };

    enum class State : uint8_t {
        Idle,
        TcpConnecting,
        MqttConnecting,
        Connected,
        Error,
    };

    /// Приоритет исходящих: управление (сеанс/присутствие/ответы) против телеметрии (state/каталог).
    enum class OutClass : uint8_t { Ctrl, Tele };

    /// Вид слота Tele: схлопывается только Delta с Delta. Full/Catalog не схлопываются.
    enum class TeleKind : uint8_t { None, Delta, Full, Catalog };

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
        /// QoS MQTT Will (0..2). Только в CONNECT.
        uint8_t willQos{1};

        uint16_t keepAliveSec{30};
        bool cleanSession{true};
    };

    struct Budgets {
        uint16_t maxReadBytesPerTick{128};
        uint16_t maxWriteBytesPerTick{128};
        uint16_t maxParseFramesPerTick{4};
        /// Предел реального времени на запись и чтение за один тик (чтобы не сработал сторожевой таймер прерываний, WDT).
        uint16_t maxMsPerTick{20};
        /// Необязательно: после pumpWrite не читать RX, если TX/CIPSEND транспорта ещё держит шину.
        bool (*shouldDeferRead)(void* ctx){nullptr};
        void* shouldDeferReadCtx{nullptr};
        /// Необязательно: блокировать Idle→CIPSTART (например, NTP / CNTP модема держит IP-стек).
        bool (*shouldBlockConnect)(void* ctx){nullptr};
        void* shouldBlockConnectCtx{nullptr};
        /// Необязательно: CIPSEND ещё в полёте (не рвать stop() посреди SEND OK).
        bool (*isTransportSendBusy)(void* ctx){nullptr};
        void* isTransportSendBusyCtx{nullptr};
        /// Необязательно: вызов сразу перед жёстким отказом на неполном кадре MQTT (место hex-дампа).
        void (*onRxIncomplete)(void* ctx, const uint8_t* head, uint16_t have, uint32_t need){nullptr};
        void* onRxIncompleteCtx{nullptr};
    };

    /// Входящий PUBLISH. При qos>0 вызывается только после постановки PUBACK в очередь.
    using PublishCb = void (*)(void* ctx, const char* topic, const uint8_t* payload, uint16_t len, bool retained);

    /// Кадр Tele вышел из очереди TX (CIPSEND подготовлен). Чтобы сдвинуть базу на пути SEND OK.
    using TeleDequeuedCb = void (*)(void* ctx, TeleKind kind);

    explicit MqttFsmClient(Client& netClient);

    void setPublishCallback(PublishCb cb, void* ctx) { _pubCb = cb; _pubCbCtx = ctx; }
    void setTeleDequeuedCallback(TeleDequeuedCb cb, void* ctx) {
        _teleDequeuedCb = cb;
        _teleDequeuedCtx = ctx;
    }

    void begin(const Config& cfg);
    void requestConnect();
    void requestDisconnect();

    // Один неблокирующий тик: двигает соединение TCP, шлёт ждущие управляющие пакеты, читает и разбирает RX.
    void tick(const Budgets& b);

    State state() const { return _state; }
    bool isConnected() const { return _state == State::Connected; }
    bool isDisconnectPending() const { return _disconnectRequested; }
    /// Истина, пока SUBSCRIBE не собран и SUBACK не увиден (или подписку не запрашивали).
    bool isSubscribePending() const { return _subRequested || _awaitingSuback; }
    /// Истина после SUBACK с выданным QoS (не 0x80). Сбрасывается при сбросе сеанса.
    bool isSubscribeConfirmed() const { return _subscribeConfirmed; }
    /// Истина, пока любой исходящий пакет MQTT в очереди или ещё пишется.
    bool hasOutbound() const { return _txQCount != 0; }
    /// Истина, если любой Ctrl (ответ/PING/online/…) в очереди или ещё отправляется.
    bool hasCtrlOutbound() const;
    /// Истина, пока несхлопываемый Full Tele в очереди или ещё отправляется.
    bool hasFullTeleOutbound() const;

    /// Принудительно состояние Error (например, переполнение RX транспорта) → путь переподключения.
    void forceError(const char* reason);
    /// Последняя причина `setError_` / `forceError` (стабильная C-строка; "" если нет).
    const char* lastErrorReason() const { return _lastErrorReason; }

    // Подписка QoS1. В состоянии Connected сразу; иначе ставится в очередь.
    bool subscribe(const char* topic);
    /// Сбросить ожидание SUBACK в полёте и снова поставить SUBSCRIBE (та же тема).
    void requeueSubscribe();

    // Публикация. `control=true` — это Ctrl. У Tele правила схлопывания задаёт `teleKind`.
    // `qos` 0 или 1 (ответы Ctrl могут быть QoS1). Кадры длиннее MQTT_CIPSEND_MAX отвергаются.
    bool publish(const char* topic, const uint8_t* payload, uint16_t len, bool retained, bool control = true,
                 TeleKind teleKind = TeleKind::Delta, uint8_t qos = 0);

    using JsonPrintEncodeFn = size_t (*)(Print& p, void* ctx);
    bool publishPrintedMeasured(const char* topic, JsonPrintEncodeFn encoder, void* ctx, uint16_t maxPayloadBytes,
                                bool retained, bool control = false, size_t* measuredBytesOut = nullptr,
                                TeleKind teleKind = TeleKind::Delta, uint8_t qos = 0);

    // millis последней активности RX (любой верный кадр MQTT).
    uint32_t lastRxMs() const { return _lastRxMs; }

private:
    Client& _net;
    Config _cfg{};
    Budgets _budgets{};
    State _state{State::Idle};

    PublishCb _pubCb{nullptr};
    void* _pubCbCtx{nullptr};
    TeleDequeuedCb _teleDequeuedCb{nullptr};
    void* _teleDequeuedCtx{nullptr};

    // Таймеры
    uint32_t _tcpConnectStartMs{0};
    uint32_t _mqttHandshakeStartMs{0};
    uint32_t _lastRxMs{0};
    uint32_t _lastTxMs{0};
    uint32_t _lastPingMs{0};
    const char* _lastErrorReason{""};

    // Ожидающие управляющие действия
    bool _connectRequested{false};
    bool _disconnectRequested{false};
    bool _disconnectFrameQueued{false}; ///< DISCONNECT уже в TX; ждём SEND OK перед stop()
    bool _subRequested{false};
    bool _awaitingSuback{false};
    bool _subscribeConfirmed{false};

    // Packet id: SUBSCRIBE и PUBLISH QoS1 делят один счётчик.
    uint16_t _nextPacketId{1};
    uint16_t _subPacketId{0};

    // Один незавершённый ответ QoS1 (копия тонкого тела, не слот очереди TX).
    bool _qos1ReplyActive{false};
    bool _qos1ReplyDupSent{false};
    uint16_t _qos1ReplyPacketId{0};
    uint16_t _qos1ReplyLen{0};
    uint32_t _qos1ReplySentMs{0};
    uint8_t _qos1ReplyCopy[NetTiming::MQTT_REPLY_QOS1_COPY_MAX]{};

    // Тема подписки в очереди (одна). Размер как TextBytes::Mqtt::TOPIC, чтобы длинные префиксы влезали.
    static constexpr size_t TOPIC_MAX = TextBytes::Mqtt::TOPIC;
    char _subTopic[TOPIC_MAX]{};

    // Очередь исходящих пакетов: кадры MQTT, готовые к отправке.
    static constexpr uint16_t TX_MAX = NetTiming::MQTT_FRAME_MAX;
    static constexpr uint8_t TX_Q_DEPTH = NetTiming::MQTT_TX_Q_DEPTH;
    uint8_t _txQ[TX_Q_DEPTH][TX_MAX]{};
    uint16_t _txQLen[TX_Q_DEPTH]{};
    OutClass _txQClass[TX_Q_DEPTH]{};
    TeleKind _txQTeleKind[TX_Q_DEPTH]{};
    uint8_t _txQHead{0};  ///< следующий пакет к отправке
    uint8_t _txQTail{0};  ///< следующий свободный слот
    uint8_t _txQCount{0};
    uint16_t _txOff{0};   ///< байтовое смещение в головном пакете, пока он пишется

    // Сборка кадра RX
    static constexpr uint16_t RX_MAX = NetTiming::MQTT_RX_ASSEMBLE_MAX;
    static constexpr uint32_t RX_ASSEMBLE_TIMEOUT_MS = NetTiming::MQTT_RX_ASSEMBLE_TIMEOUT_MS;
    uint8_t _rx[RX_MAX]{};
    uint16_t _rxLen{0};
    uint32_t _rxAssembleStartMs{0};

    void resetSession_();
    void setError_(const char* reason);
    void clearTxQueue_();
    void clearQos1Reply_();
    void tickQos1Reply_(uint32_t now);
    uint16_t allocPacketId_();
    bool queueHasRoom_() const { return _txQCount < TX_Q_DEPTH; }
    uint8_t freeSlots_() const { return (uint8_t)(TX_Q_DEPTH - _txQCount); }
    bool hasCtrlInQueue_() const;
    /// Индекс заменяемого слота Delta Tele, или -1. Пропускает отправляемую голову, Full и Catalog.
    int findReplaceableDeltaTele_() const;
    /// Индекс Full Tele, который более новый Full может перезаписать, или -1.
    int findReplaceableFullTele_() const;
    bool beginQueueSlot_(OutClass cls, uint8_t*& buf, uint16_t& cap);
    void commitQueueSlot_(uint16_t len, OutClass cls, TeleKind teleKind = TeleKind::None);
    bool beginReplaceTeleSlot_(TeleKind wantKind, uint8_t*& buf, uint16_t& cap, uint8_t& slotIdxOut);
    void commitReplaceTeleSlot_(uint8_t slotIdx, uint16_t len, TeleKind kind);

    bool ensureTcp_();
    void maybeSendPing_(uint32_t now);
    void maybeKeepaliveWatchdog_(uint32_t now);
    void pumpWrite_(uint16_t maxBytes, uint32_t deadlineMs);
    void pumpReadAndParse_(uint16_t maxBytes, uint16_t maxFrames, uint32_t deadlineMs);
    bool resyncDropOne_(const char* why);
    static bool pastDeadline_(uint32_t deadlineMs);

    bool buildConnect_();
    bool buildDisconnect_();
    bool buildPingreq_();
    bool buildSubscribe_(const char* topic);
    bool buildPublish_(const char* topic, const uint8_t* payload, uint16_t len, bool retained, OutClass cls,
                       TeleKind teleKind, uint8_t qos);

    bool parseOneFrame_();
};
