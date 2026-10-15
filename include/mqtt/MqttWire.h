// include/mqtt/MqttWire.h
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * Кодирование и разбор пакетов MQTT 3.1/3.1.1 над байтовыми буферами (без Arduino и без состояния,
 * тестируется на хосте). Очередь, тайминги и сеанс — в MqttFsmClient; см. docs/modules/mqtt.md.
 */
namespace mqtt_wire {

/// Наибольшее значение Remaining Length (4 байта по 7 бит).
constexpr uint32_t REMAINING_LENGTH_MAX = 268435455UL;

/// Тип пакета (старшие 4 бита первого байта).
namespace packet {
constexpr uint8_t Connect = 1;
constexpr uint8_t Connack = 2;
constexpr uint8_t Publish = 3;
constexpr uint8_t Puback = 4;
constexpr uint8_t Subscribe = 8;
constexpr uint8_t Suback = 9;
constexpr uint8_t Pingreq = 12;
constexpr uint8_t Pingresp = 13;
constexpr uint8_t Disconnect = 14;
} // namespace packet

/// Кодирует Remaining Length; возвращает 1..4 байта или 0, если `len` больше REMAINING_LENGTH_MAX.
uint8_t encodeRemainingLength(uint32_t len, uint8_t out[4]);

/// Число байтов, которое займёт Remaining Length для `len` (0 — недопустимая длина).
uint8_t remainingLengthSize(uint32_t len);

enum class Decode : uint8_t { Ok, NeedMore, Malformed };

/// Читает Remaining Length из `p[0..cap)`. Malformed — четыре байта с битом продолжения.
Decode decodeRemainingLength(const uint8_t* p, uint16_t cap, uint32_t& outLen, uint8_t& outUsed);

/// Строка MQTT: 2 байта длины + байты. 0 — не помещается в `cap` (строка не обрезается).
uint16_t putString(uint8_t* p, uint16_t cap, const char* s);

struct FrameHead {
    uint8_t type{0};
    uint8_t flags{0};
    uint16_t headerLen{0};  ///< 1 + байты Remaining Length
    uint32_t remLen{0};
    uint32_t totalLen{0};   ///< headerLen + remLen
};

enum class Head : uint8_t { Ok, NeedMore, BadType, BadRemLen };

/// Фиксированный заголовок в начале `buf`. Тип вне 1..14 — BadType.
Head parseHead(const uint8_t* buf, uint16_t len, FrameHead& out);

/// В `buf` правильный заголовок, но байтов меньше, чем нужно кадру (и кадр поместится в `capacity`).
bool isValidIncomplete(const uint8_t* buf, uint16_t len, uint16_t capacity, uint32_t& needOut);

struct ConnectOptions {
    bool mqtt311{true};          ///< false — MQTT 3.1 ("MQIsdp", уровень 3)
    const char* clientId{nullptr};
    const char* username{nullptr};
    const char* password{nullptr};  ///< передаётся только вместе с username (MQTT 3.1.1 §3.1.2.9)
    const char* willTopic{nullptr};
    const char* willMsg{nullptr};
    bool willRetain{false};
    uint8_t willQos{0};
    uint16_t keepAliveSec{30};
    bool cleanSession{true};
};

/// Билдеры возвращают длину пакета в `out` или 0, если он не помещается / параметры недопустимы.
/// При 0 буфер не тронут: так можно безопасно писать поверх пакета, уже стоящего в очереди.
uint16_t buildConnect(uint8_t* out, uint16_t cap, const ConnectOptions& o);
uint16_t buildSubscribe(uint8_t* out, uint16_t cap, uint16_t packetId, const char* topic);
uint16_t buildPublish(uint8_t* out, uint16_t cap, const char* topic, const uint8_t* payload, uint16_t len,
                      bool retained);
uint16_t buildPuback(uint8_t* out, uint16_t cap, uint16_t packetId);
uint16_t buildPingreq(uint8_t* out, uint16_t cap);
uint16_t buildDisconnect(uint8_t* out, uint16_t cap);

/// Длина заголовка PUBLISH QoS0 (фиксированный заголовок + тема) для нагрузки `payloadLen`.
uint16_t publishHeadLength(uint16_t topicLen, uint32_t payloadLen);
/// Пишет заголовок PUBLISH QoS0 и тему; возвращает число байтов или 0.
uint16_t writePublishHead(uint8_t* out, uint16_t cap, const char* topic, uint16_t topicLen, uint32_t payloadLen,
                          bool retained);

/// Тело CONNACK (после фиксированного заголовка).
bool parseConnack(const uint8_t* body, uint32_t remLen, uint8_t& returnCode);
/// Тело SUBACK с одним кодом.
bool parseSuback(const uint8_t* body, uint32_t remLen, uint16_t& packetId, uint8_t& returnCode);

struct PublishView {
    uint16_t topicOffset{0};    ///< от начала тела
    uint16_t topicLen{0};
    uint16_t payloadOffset{0};  ///< от начала тела
    uint16_t payloadLen{0};
    uint8_t qos{0};
    bool retained{false};
    uint16_t packetId{0};       ///< только при qos > 0
};

/// Тело входящего PUBLISH. Пустая тема, тема за границей, QoS 3 или нет места под packet id — false.
bool parsePublish(uint8_t flags, const uint8_t* body, uint32_t remLen, PublishView& out);

} // namespace mqtt_wire
