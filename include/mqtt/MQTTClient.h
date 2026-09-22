// include/mqtt/MQTTClient.h
#pragma once

#include <Arduino.h>
#include <Client.h>

#include "common/Constants.h"

struct AppPorts;
#include "mqtt/MqttFsmClient.h"

/**
 * @brief MQTT-клиент для связи с брокером.
 * Публикует статус и обрабатывает входящие команды.
 *
 * Контракт топиков (из `mqtt.topic_prefix`):
 * - `{prefix}/avail`  — retained online/offline (LWT)
 * - `{prefix}/status` — JSON телеметрия
 * - `{prefix}/cmd`    — входящие команды
 * - `{prefix}/reply`  — ответы (list_programs)
 */
class MQTTClient {
public:
    // Конструктор, принимает уже выбранный сетевой клиент.
    MQTTClient(Client& client);

    // Настройка из конфига и подключение к брокеру
    void begin();

    /// Optional app wiring (commands/status) provided by `core` after construction.
    void setAppPorts(const AppPorts* ports) { _ports = ports; }

    /// Optional probe: skip MQTT RX while modem TX/CIPSEND owns the bus (wired from CellularCore).
    void setDeferMqttRx(bool (*fn)(void*), void* ctx) {
        _deferMqttRx = fn;
        _deferMqttRxCtx = ctx;
    }

    // Циклический вызов (поддержка соединения, обработка команд)
    void loop();

    /// Разрешить/запретить попытки подключения/переподключения к брокеру.
    void setReconnectEnabled(bool enabled) { _reconnectEnabled = enabled; }

    // Публикация статуса устройства в топик статуса
    bool publishStatus();

    // Принудительное отключение от брокера
    void disconnect();

    // Количество подряд неуспешных connect() попыток (для health-триггера).
    uint8_t getConsecutiveConnectFails() const;

    // True when MQTT backend is non-blocking (safe to keep enabled during active UI/SSE).
    bool isNonBlocking() const { return true; }

private:
    Client& _netClient;             ///< ссылка на сетевой клиент
    uint32_t _lastStatusPublish;    ///< время последней публикации статуса (мс)
    uint32_t _lastReconnectAttempt; ///< время последней попытки переподключения (мс)
    bool _reconnectEnabled{true};
    uint8_t _connectFailStreak{0};
    const AppPorts* _ports{nullptr};
    bool (*_deferMqttRx)(void*){nullptr};
    void* _deferMqttRxCtx{nullptr};

    // Non-blocking MQTT FSM engine.
    MqttFsmClient _fsm;
    bool _subscribed{false};
    bool _mqttWasConnected{false}; ///< edge-detect: prime first status publish after each (re)connect
    bool _onlinePublishDue{false}; ///< retained "online" on avail after each CONNECT
    bool _awaitFirstStatus{false}; ///< one JSON status after settle on each session
    bool _listProgramsDue{false};  ///< retry list_programs reply when TX was busy
    uint32_t _firstStatusAfterMs{0}; ///< earliest millis() for `_awaitFirstStatus`

    /// NUL-terminated MQTT credentials (copied once in `begin()` from config).
    char _mqttClientId[TextBytes::Mqtt::CLIENT_ID]{};
    char _mqttUser[TextBytes::Mqtt::USER]{};
    char _mqttPass[TextBytes::Mqtt::PASS]{};

    /// Derived topics from `mqtt.topic_prefix` (stable pointers for CONNECT Will).
    char _topicAvail[TextBytes::Mqtt::TOPIC]{};
    char _topicStatus[TextBytes::Mqtt::TOPIC]{};
    char _topicCmd[TextBytes::Mqtt::TOPIC]{};
    char _topicReply[TextBytes::Mqtt::TOPIC]{};

    void connect();
    static void joinTopic_(char* out, size_t outSz, const char* prefix, const char* suffix);
    bool tryPublishProgramList_();

    static void onPublishThunk(void* ctx, const char* topic, const uint8_t* payload, uint16_t len, bool retained);
    void handlePublish(const char* topic, const uint8_t* payload, uint16_t len, bool retained);
};
