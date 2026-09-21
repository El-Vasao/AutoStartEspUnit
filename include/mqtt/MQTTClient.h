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
 * Контракт:
 * - работает поверх уже готового сетевого транспорта (`Client`), в этой прошивке — через GSM;
 * - `begin()` читает настройки из `config`, настраивает брокер и выполняет первое подключение;
 * - `loop()` должен вызываться только когда внешний транспорт уже готов; внутри реализованы reconnect/publish по таймеру;
 * - при потере транспорта внешний orchestration обязан вызвать `disconnect()`.
 *
 * Замечания для ESP8266:
 * - Реализация неблокирующая (FSM), без динамических аллокаций.
 * - Payloadы ограничены фиксированными буферами (см. JsonBytes::Mqtt::*).
 */
class MQTTClient {
public:
    // Конструктор, принимает уже выбранный сетевой клиент.
    MQTTClient(Client& client);

    // Настройка из конфига и подключение к брокеру
    void begin();

    /// Optional app wiring (commands/status) provided by `core` after construction.
    void setAppPorts(const AppPorts* ports) { _ports = ports; }

    // Циклический вызов (поддержка соединения, обработка команд)
    void loop();

    /// Разрешить/запретить попытки подключения/переподключения к брокеру.
    /// Важно для UX: даже неблокирующий MQTT может давать лишнюю нагрузку на сеть/модем,
    /// поэтому при желании его можно выключать во время активного UI (AP/SSE).
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

    // Non-blocking MQTT FSM engine.
    MqttFsmClient _fsm;
    bool _subscribed{false};
    bool _mqttWasConnected{false}; ///< edge-detect: prime first status publish after each (re)connect
    bool _onlinePublishDue{false}; ///< retained "online" on `status_topic` after each CONNECT
    bool _awaitFirstStatus{false}; ///< one JSON status after online/settle on each session
    uint32_t _firstStatusAfterMs{0}; ///< earliest millis() for `_awaitFirstStatus`

    /// NUL-terminated MQTT credentials (copied once in `begin()` from config).
    char _mqttClientId[TextBytes::Mqtt::CLIENT_ID]{};
    char _mqttUser[TextBytes::Mqtt::USER]{};
    char _mqttPass[TextBytes::Mqtt::PASS]{};
    /// Copy of configured `mqtt.status_topic` for Last Will pointer stability in `MqttFsmClient::Config`.
    char _mqttStatusTopic[TextBytes::Mqtt::TOPIC]{};

    // Подключение к брокеру (внутреннее)
    void connect();

    static void onPublishThunk(void* ctx, const char* topic, const uint8_t* payload, uint16_t len, bool retained);
    void handlePublish(const char* topic, const uint8_t* payload, uint16_t len, bool retained);
};

