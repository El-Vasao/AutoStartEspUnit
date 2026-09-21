#include "mqtt/MQTTClient.h"

#include "app/AppPorts.h"
#include "config/Config.h"
#include "mqtt/MqttCommandParser.h"
#include "mqtt/MqttStatusBuilder.h"

#include "app/StatusSnapshot.h"
#include <WiFi.h>
#include <cstring>
#include "common/Constants.h"
#include "common/EspHal.h"
#include "common/Logger.h"
#include "common/Pins.h"
#include "common/Version.h"
#include "core/Core.h"
#include "io/SensorsController.h"
#include "io/RelayController.h"
#include "io/DigitalInputs.h"
#include "program/ProgramExecutor.h"
#include "core/ErrorManager.h"

namespace {

struct CountingForwarder final : public Print {
    Print& d;
    size_t n = 0;
    explicit CountingForwarder(Print& x) : d(x) {}

    size_t write(uint8_t b) override {
        const size_t w = d.write(b);
        n += w;
        return w;
    }
    size_t write(const uint8_t* buf, size_t s) override {
        const size_t w = d.write(buf, s);
        n += w;
        return w;
    }
    size_t written() const { return n; }
};

struct PublishStatusCtx {
    const StatusSnapshot* s;
};

static void buildStatusSnapshotForMqtt(StatusSnapshot& s) {
    s = StatusSnapshot{};
    s.uptimeSec = core.getUptime();
    strlcpy(s.modeName, core.getModeName(), sizeof(s.modeName));

    auto& sensors = core.getSensors();
    auto& relay = core.getRelay();
    auto& inputs = core.getInputs();
    auto& pe = core.getProgramExecutor();
    auto& err = core.getErrorManager();

    s.voltageValid = sensors.isVoltageValid();
    s.voltage = s.voltageValid ? sensors.getVoltage() : 0.0f;
    s.engineRunning = core.isEngineRunning();

    const auto& cfg = config.getBase();
    for (int i = 0; i < HardwareLimits::SENSORS; i++) {
        uint16_t sid = 0;
        const uint8_t* addr = sensors.getSensorAddress(i);
        if (addr) {
            char romStr[TextBytes::Sensors::ADDR_STRING];
            snprintf(romStr, sizeof(romStr), "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2],
                     addr[3], addr[4], addr[5], addr[6], addr[7]);
            for (uint8_t j = 0; j < HardwareLimits::SENSORS; j++) {
                const auto& sc = cfg.sensors[j];
                if (sc.rom[0] == '\0' || sc.id == 0) continue;
                if (strcmp(sc.rom, romStr) == 0) {
                    sid = sc.id;
                    break;
                }
            }
        }
        s.tempSensors[i].id = sid;
        s.tempSensors[i].valid = sensors.isTemperatureValid(i);
        s.tempSensors[i].lastMs = sensors.getLastTemperatureTime(i);
        s.tempSensors[i].t = s.tempSensors[i].valid ? sensors.getTemperature(i) : 0.0f;
    }

    for (int i = 0; i < HardwareLimits::RELAYS; i++) {
        s.relayState[i] = relay.getState(i);
    }
    for (int i = 0; i < HardwareLimits::INPUTS; i++) {
        s.inputState[i] = inputs.getState(i);
    }

    s.programRunning = pe.isRunning();
    s.currentProgramId = s.programRunning ? pe.getCurrentProgramId() : 0;
    s.lastProgramId = pe.getLastProgramId();

    for (uint8_t i = 0; i < Limits::MAX_TRIGGERS; i++) {
        s.inputTriggerRuntime[i] = false;
        s.tempTriggerRuntime[i] = false;
    }
    for (uint8_t i = 0; i < cfg.input_triggers_count && i < Limits::MAX_TRIGGERS; i++) {
        s.inputTriggerRuntime[i] = core.getTriggerRuntime(i);
    }
    for (uint8_t i = 0; i < cfg.temperature_triggers_count && i < Limits::MAX_TRIGGERS; i++) {
        s.tempTriggerRuntime[i] = core.getTempTriggerRuntime(i);
    }

    strlcpy(s.lastError, err.getMessage(), sizeof(s.lastError));
}

static size_t encodeMqttStatusForPublish(Print& p, void* ctx) {
    CountingForwarder fc(p);
    auto* c = reinterpret_cast<const PublishStatusCtx*>(ctx);
    emitMqttStatusJson(*c->s, config.getBase(), fc);
    return fc.written();
}

static size_t encodeProgramListForPublish(Print& p, void*) {
    CountingForwarder fc(p);
    if (!config.emitProgramListWrapped(fc)) return 0;
    return fc.written();
}

} // namespace

static constexpr uint32_t kMqttPublishBudgetMs = 10;

MQTTClient::MQTTClient(Client& client)
    : _netClient(client), _lastStatusPublish(0), _lastReconnectAttempt(0), _fsm(client)
{
    _fsm.setPublishCallback(&MQTTClient::onPublishThunk, this);
}

uint8_t MQTTClient::getConsecutiveConnectFails() const {
    return _connectFailStreak;
}

void MQTTClient::begin() {
    logger.log("[MQTTClient] begin()\n");
    const auto& mqttCfg = config.getBase().mqtt;

    // Make defensive, NUL-terminated copies of config strings.
    // Some config fields may be filled from JSON without guaranteed full NUL-termination
    // (e.g. if source length == buffer size). MQTT strings must be well-formed.
    const char* clientId = mqttCfg.client_id;
    if (!clientId || clientId[0] == '\0') {
        snprintf(_mqttClientId, sizeof(_mqttClientId), "autostart-%06X", (unsigned)espHalChipId());
    } else {
        strlcpy(_mqttClientId, clientId, sizeof(_mqttClientId));
    }
    strlcpy(_mqttUser, mqttCfg.user, sizeof(_mqttUser));
    strlcpy(_mqttPass, mqttCfg.pass, sizeof(_mqttPass));
    strlcpy(_mqttStatusTopic, mqttCfg.status_topic, sizeof(_mqttStatusTopic));

    // Diagnostics: confirm exact credentials lengths (do NOT print password).
    logger.log("[MQTTClient] cfg: clientIdLen=%u userLen=%u passLen=%u\n",
               (unsigned)strlen(_mqttClientId),
               (unsigned)strlen(_mqttUser),
               (unsigned)strlen(_mqttPass));
    logger.log("[MQTTClient] cfg: user=%s\n", _mqttUser);

    MqttFsmClient::Config c{};
    c.host = mqttCfg.broker;
    c.port = mqttCfg.port;
#if defined(MQTT_VERSION) && (MQTT_VERSION == MQTT_VERSION_3_1)
    c.proto = MqttFsmClient::Proto::Mqtt31;
#else
    c.proto = MqttFsmClient::Proto::Mqtt311;
#endif
    c.clientId = _mqttClientId;
    c.username = (_mqttUser[0] ? _mqttUser : nullptr);
    c.password = (_mqttPass[0] ? _mqttPass : nullptr);
    // Last Will on status topic so subscribers see retained "offline" on unclean disconnect.
    static constexpr char kOffline[] = "offline";
    if (_mqttStatusTopic[0]) {
        c.willTopic = _mqttStatusTopic;
        c.willMsg = kOffline;
        c.willRetain = true;
        c.willQos = 1;
    } else {
        c.willTopic = nullptr;
        c.willMsg = nullptr;
        c.willRetain = false;
        c.willQos = 0;
    }
    c.keepAliveSec = 30;
    c.cleanSession = true;
    _fsm.begin(c);

    logger.log("[MQTTClient] settings: keepAlive=%us proto=%s\n",
               (unsigned)30,
               (c.proto == MqttFsmClient::Proto::Mqtt31) ? "3.1" : "3.1.1");
    if (!_reconnectEnabled) {
        logger.log("[MQTTClient] begin(): reconnect disabled, skipping initial connect\n");
    } else {
        // Kick off immediately: do not wait for the first reconnect interval tick.
        _lastReconnectAttempt = millis();
        connect();
    }
}

void MQTTClient::loop() {
    if (!_reconnectEnabled) return;

    const uint32_t now = millis();
    // Reconnect only from Idle/Error when no MQTT TX is staged (single in-flight packet).
    if (!_fsm.isConnected() && (now - _lastReconnectAttempt > NetTiming::MQTT_RECONNECT_INTERVAL_MS)) {
        if (_fsm.state() == MqttFsmClient::State::Idle || _fsm.state() == MqttFsmClient::State::Error) {
            _lastReconnectAttempt = now;
            connect();
        }
    }

    const auto& mqttCfg = config.getBase().mqtt;

    // Queue SUBSCRIBE before tick so the same iteration can build/send it (otherwise SUB is delayed one loop).
    if (_fsm.isConnected() && !_subscribed) {
        _subscribed = _fsm.subscribe(mqttCfg.cmd_topic);
        if (_subscribed) {
            logger.log("[MQTTClient] Connected. subscribe=%s status=%s\n", mqttCfg.cmd_topic, mqttCfg.status_topic);
        }
    }

    MqttFsmClient::Budgets b{};
    b.maxReadBytesPerTick = 160;
    b.maxWriteBytesPerTick = 160;
    b.maxParseFramesPerTick = 4;
    _fsm.tick(b);

    if (_fsm.isConnected()) {
        if (!_mqttWasConnected) {
            _mqttWasConnected = true;
            _connectFailStreak = 0;
            _lastStatusPublish = 0;
            _onlinePublishDue = mqttCfg.status_topic[0];
            _awaitFirstStatus = true;
            _firstStatusAfterMs = millis() + 1500u;
            core.logHeapSnapshot("mqtt_online");
        }

        static constexpr uint8_t kOnlinePayload[] = "online";
        if (_onlinePublishDue && mqttCfg.status_topic[0]) {
            if (_fsm.publish(mqttCfg.status_topic, kOnlinePayload, sizeof(kOnlinePayload) - 1u, true)) {
                _onlinePublishDue = false;
                _firstStatusAfterMs = millis() + 1500u;
            }
        } else if (_awaitFirstStatus) {
            if ((int32_t)(millis() - _firstStatusAfterMs) >= 0 && publishStatus()) {
                _awaitFirstStatus = false;
                _lastStatusPublish = millis();
                core.cooperate();
            }
        } else {
            const uint32_t interval_ms = mqttCfg.publish_interval_sec * 1000UL;
            const bool due = (_lastStatusPublish == 0) || (millis() - _lastStatusPublish >= interval_ms);
            if (due && publishStatus()) {
                _lastStatusPublish = millis();
                core.cooperate();
            }
        }
    } else {
        // Reset sticky per-connection actions when disconnected.
        _subscribed = false;
        _mqttWasConnected = false;
        _onlinePublishDue = false;
        _awaitFirstStatus = false;
    }
}

void MQTTClient::disconnect() {
    logger.log("[MQTTClient] disconnect()\n");
    _fsm.requestDisconnect();
    _subscribed = false;
}

void MQTTClient::connect() {
    const auto& mqttCfg = config.getBase().mqtt;
    if (_fsm.state() == MqttFsmClient::State::Error) {
        if (_connectFailStreak < 255) _connectFailStreak++;
    }
    if (_fsm.state() == MqttFsmClient::State::Idle) {
        logger.log("[MQTTClient] connect() broker=%s:%u\n", mqttCfg.broker, (unsigned)mqttCfg.port);
    }
    _fsm.requestConnect();
}

void MQTTClient::onPublishThunk(void* ctx, const char* topic, const uint8_t* payload, uint16_t len, bool retained) {
    (void)retained;
    if (!ctx) return;
    ((MQTTClient*)ctx)->handlePublish(topic, payload, len, retained);
}

void MQTTClient::handlePublish(const char* topic, const uint8_t* payload, uint16_t length, bool retained) {
    (void)retained;
    logger.logSerialOnly("[MQTTClient] cmd recv topic=%s len=%u\n", topic ? topic : "", (unsigned)length);
    char message[JsonBytes::Mqtt::CMD_JSON_MAX];
    if (length == 0 || length >= sizeof(message)) {
        logger.log("[MQTTClient] Command payload too large (%u)\n", (unsigned)length);
        return;
    }
    memcpy(message, payload, length);
    message[length] = '\0';

    MqttCommand cmd;
    if (!parseMqttCommandJson(message, cmd)) {
        logger.log("[MQTTClient] Unsupported/invalid command\n");
        return;
    }

    core.logHeapSnapshot("mqtt_cmd");
    if (cmd.kind == MqttCommandKind::RunProgram) {
        logger.log("[MQTTClient] Run program %u\n", (unsigned)cmd.programId);
        if (_ports && _ports->control.startProgram) {
            _ports->control.startProgram(_ports->control.ctx, cmd.programId);
        } else {
            logger.log("[MQTTClient] run: AppPorts not wired\n");
        }
    } else if (cmd.kind == MqttCommandKind::ListPrograms) {
        const auto& mqttCfg = config.getBase().mqtt;
        size_t measured = 0;
        if (!_fsm.publishPrintedMeasured(mqttCfg.status_topic, encodeProgramListForPublish, nullptr,
                                         JsonBytes::Mqtt::LIST_PROGRAMS_JSON_MAX, false, &measured)) {
            if (measured > JsonBytes::Mqtt::LIST_PROGRAMS_JSON_MAX) {
                logger.log("[MQTTClient] list_programs JSON too large for MQTT buffer (max %u, got %u)\n",
                           (unsigned)JsonBytes::Mqtt::LIST_PROGRAMS_JSON_MAX, (unsigned)measured);
            } else {
                logger.log("[MQTTClient] list_programs publish failed (busy/not connected/wire); measured=%u\n",
                           (unsigned)measured);
            }
        }
        logger.log("[MQTTClient] list_programs done\n");
    } else {
        logger.log("[MQTTClient] Unknown command kind\n");
    }
}

bool MQTTClient::publishStatus() {
    const auto& mqttCfg = config.getBase().mqtt;
    StatusSnapshot snapshot{};
    buildStatusSnapshotForMqtt(snapshot);
    PublishStatusCtx ctx{&snapshot};
    size_t measured = 0;
    const uint32_t t0 = millis();
    if (!_fsm.publishPrintedMeasured(mqttCfg.status_topic, encodeMqttStatusForPublish, &ctx,
                                     JsonBytes::Mqtt::STATUS_PAYLOAD_MAX_BYTES, false, &measured)) {
        if (measured > JsonBytes::Mqtt::STATUS_PAYLOAD_MAX_BYTES) {
            logger.log("[MQTTClient] publishStatus: status JSON too large for MQTT buffer (max %u, got %u)\n",
                       (unsigned)JsonBytes::Mqtt::STATUS_PAYLOAD_MAX_BYTES, (unsigned)measured);
        } else {
            logger.log("[MQTTClient] publishStatus failed (busy/not connected/wire); measured=%u max=%u\n",
                       (unsigned)measured, (unsigned)JsonBytes::Mqtt::STATUS_PAYLOAD_MAX_BYTES);
        }
        return false;
    }
    const uint32_t dt = millis() - t0;
    if (dt > kMqttPublishBudgetMs) {
        logger.log("[MQTTClient] status publish serialize+stage slow (%u ms) (> budget %u ms)\n", (unsigned)dt,
                   (unsigned)kMqttPublishBudgetMs);
    }
    logger.log("[MQTTClient] status published topic=%s bytes=%u\n", mqttCfg.status_topic, (unsigned)measured);
    return true;
}
