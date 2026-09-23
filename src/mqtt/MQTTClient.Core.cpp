#include "mqtt/MQTTClient.h"

#include "app/AppPorts.h"
#include "config/Config.h"
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
    const StatusSnapshot* cur;
    const StatusSnapshot* prev; ///< non-null when emitting a delta
    bool full;
};

static size_t encodeMqttStatusForPublish(Print& p, void* ctx) {
    CountingForwarder fc(p);
    auto* c = reinterpret_cast<const PublishStatusCtx*>(ctx);
    if (c->full || !c->prev) {
        emitMqttStatusJson(*c->cur, config.getBase(), fc);
    } else {
        emitMqttStatusDeltaJson(*c->cur, *c->prev, config.getBase(), fc);
    }
    return fc.written();
}

} // namespace

void MQTTClient::captureStatusSnapshot_(StatusSnapshot& s) const {
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

static constexpr uint32_t kMqttPublishBudgetMs = 10;

MQTTClient::MQTTClient(Client& client)
    : _netClient(client), _lastStatusPublish(0), _lastReconnectAttempt(0), _fsm(client)
{
    _fsm.setPublishCallback(&MQTTClient::onPublishThunk, this);
}

uint8_t MQTTClient::getConsecutiveConnectFails() const {
    return _connectFailStreak;
}

void MQTTClient::joinTopic_(char* out, size_t outSz, const char* prefix, const char* suffix) {
    if (!out || outSz == 0) return;
    out[0] = '\0';
    if (!prefix || !prefix[0] || !suffix || !suffix[0]) return;
    snprintf(out, outSz, "%s/%s", prefix, suffix);
}

void MQTTClient::begin() {
    const auto& mqttCfg = config.getBase().mqtt;

    // Make defensive, NUL-terminated copies of config strings.
    const char* clientId = mqttCfg.client_id;
    if (!clientId || clientId[0] == '\0') {
        snprintf(_mqttClientId, sizeof(_mqttClientId), "autostart-%06X", (unsigned)espHalChipId());
    } else {
        strlcpy(_mqttClientId, clientId, sizeof(_mqttClientId));
    }
    strlcpy(_mqttUser, mqttCfg.user, sizeof(_mqttUser));
    strlcpy(_mqttPass, mqttCfg.pass, sizeof(_mqttPass));

    joinTopic_(_topicAvail, sizeof(_topicAvail), mqttCfg.topic_prefix, MqttTopics::AVAIL);
    joinTopic_(_topicStatus, sizeof(_topicStatus), mqttCfg.topic_prefix, MqttTopics::STATUS);
    joinTopic_(_topicCmd, sizeof(_topicCmd), mqttCfg.topic_prefix, MqttTopics::CMD);
    joinTopic_(_topicReply, sizeof(_topicReply), mqttCfg.topic_prefix, MqttTopics::REPLY);

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
    // Last Will on avail so subscribers see retained "offline" on unclean disconnect.
    static constexpr char kOffline[] = "offline";
    if (_topicAvail[0]) {
        c.willTopic = _topicAvail;
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

    // One-line begin: lengths only for credentials (no plaintext pass on SSE).
    logger.log("[MQTTClient] begin keepAlive=30s proto=%s idLen=%u userLen=%u passLen=%u "
               "avail=%s status=%s cmd=%s reply=%s\n",
               (c.proto == MqttFsmClient::Proto::Mqtt31) ? "3.1" : "3.1.1",
               (unsigned)strlen(_mqttClientId),
               (unsigned)strlen(_mqttUser),
               (unsigned)strlen(_mqttPass),
               _topicAvail, _topicStatus, _topicCmd, _topicReply);
    logger.logSerialOnly("[MQTTClient] cfg: user=%s prefix=%s\n", _mqttUser, mqttCfg.topic_prefix);

    if (!_reconnectEnabled) {
        logger.log("[MQTTClient] begin(): reconnect disabled, skipping initial connect\n");
    } else {
        _lastReconnectAttempt = millis();
        connect();
    }
}

void MQTTClient::onTransportRxOverflow() {
    logger.log("[MQTTClient] transport RX overflow — forcing reconnect\n");
    _fsm.forceError("rx_overflow");
}

void MQTTClient::loop() {
    // Still drain clean disconnect even when reconnect is disabled (suspend path).
    if (!_reconnectEnabled && !_fsm.isDisconnectPending()) return;

    const uint32_t now = millis();
    if (_reconnectEnabled && !_fsm.isConnected() &&
        (now - _lastReconnectAttempt > NetTiming::MQTT_RECONNECT_INTERVAL_MS)) {
        if (_fsm.state() == MqttFsmClient::State::Idle || _fsm.state() == MqttFsmClient::State::Error) {
            _lastReconnectAttempt = now;
            connect();
        }
    }

    MqttFsmClient::Budgets b{};
    b.maxReadBytesPerTick = 256;
    b.maxWriteBytesPerTick = 1024;
    b.maxParseFramesPerTick = 4;
    b.maxMsPerTick = 20;
    // Always drain RX ring (shouldDeferMqttRead is false on SIM800).
    b.shouldDeferRead = _deferMqttRx;
    b.shouldDeferReadCtx = _deferMqttRxCtx;
    _fsm.tick(b);

    if (_fsm.isConnected()) {
        // Queue SUBSCRIBE once; confirmed only after SUBACK.
        if (!_subscribeQueued && _topicCmd[0]) {
            if (_fsm.subscribe(_topicCmd)) {
                _subscribeQueued = true;
                _subWaitStartMs = millis();
                _subackTimeoutLogged = false;
                logger.log("[MQTTClient] Connected. subscribe=%s status=%s avail=%s\n",
                           _topicCmd, _topicStatus, _topicAvail);
            }
        }

        if (!_mqttWasConnected) {
            _mqttWasConnected = true;
            _connectFailStreak = 0;
            _lastStatusPublish = 0;
            _onlinePublishDue = _topicAvail[0] != '\0';
            _awaitFirstStatus = _topicStatus[0] != '\0';
            _firstStatusAfterMs = millis() + 1500u;
            _havePublishedBaseline = false;
        }

        static constexpr uint32_t kSubackTimeoutMs = 8000u;
        const bool subConfirmed = _fsm.isSubscribeConfirmed();
        const bool subPending = _topicCmd[0] && _fsm.isSubscribePending();
        const bool subTimedOut =
            subPending && !subConfirmed && _subWaitStartMs != 0 &&
            (millis() - _subWaitStartMs) >= kSubackTimeoutMs;
        if (subTimedOut) {
            if (!_subackTimeoutLogged) {
                _subackTimeoutLogged = true;
                logger.log("[MQTTClient] SUBACK timeout %ums — publishing online/status; retrying SUBSCRIBE\n",
                           (unsigned)kSubackTimeoutMs);
            }
            _fsm.requeueSubscribe();
            _subWaitStartMs = millis();
        }
        // Presence/tele after SUBACK, or after timeout so we are not stuck offline forever.
        const bool subReady = subConfirmed || subTimedOut || _subackTimeoutLogged;

        static constexpr uint8_t kOnlinePayload[] = "online";
        if (subReady && _onlinePublishDue && _topicAvail[0]) {
            if (_fsm.publish(_topicAvail, kOnlinePayload, sizeof(kOnlinePayload) - 1u, true, true)) {
                _onlinePublishDue = false;
                logger.log("[MQTTClient] pub avail online\n");
            }
        }

        flushPending_();
        drainInbound_();

        const bool teleIdle = (_inboundCount == 0) && (_pendingCount == 0) && !_cmdBusy && !_fsm.hasCtrlOutbound();

        if (subReady && teleIdle && _awaitFirstStatus) {
            if ((int32_t)(millis() - _firstStatusAfterMs) >= 0 && publishStatus(true)) {
                _awaitFirstStatus = false;
                _lastStatusPublish = millis();
                core.cooperate();
            }
        } else if (subReady && teleIdle && _topicStatus[0]) {
            const auto& mqttCfg = config.getBase().mqtt;
            const uint32_t interval_ms = mqttCfg.publish_interval_sec * 1000UL;
            const bool due = (_lastStatusPublish == 0) || (millis() - _lastStatusPublish >= interval_ms);
            if (due && publishStatus(false)) {
                _lastStatusPublish = millis();
                core.cooperate();
            }
        }
    } else if (!_fsm.isDisconnectPending()) {
        _subscribeQueued = false;
        _mqttWasConnected = false;
        _onlinePublishDue = false;
        _awaitFirstStatus = false;
        _havePublishedBaseline = false;
        _subWaitStartMs = 0;
        _subackTimeoutLogged = false;
        clearPending_();
        clearInbound_();
        _hasActiveRun = false;
        _activeRunId[0] = '\0';
        _cmdBusy = false;
    }
}

void MQTTClient::disconnect() {
    logger.log("[MQTTClient] disconnect()\n");
    if (_fsm.isConnected() && _topicAvail[0]) {
        static constexpr uint8_t kOfflinePayload[] = "offline";
        (void)_fsm.publish(_topicAvail, kOfflinePayload, sizeof(kOfflinePayload) - 1u, true, true);
    }
    _fsm.requestDisconnect();
    _subscribeQueued = false;
    _subWaitStartMs = 0;
    _subackTimeoutLogged = false;
    clearPending_();
    clearInbound_();
    _hasActiveRun = false;
    _activeRunId[0] = '\0';
}

bool MQTTClient::needsDisconnectDrain() const {
    return _fsm.isDisconnectPending() || _fsm.hasOutbound();
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

bool MQTTClient::publishStatus(bool forceFull) {
    if (!_topicStatus[0]) return false;

    StatusSnapshot snapshot{};
    captureStatusSnapshot_(snapshot);
    const auto& cfg = config.getBase();

    // Full until first successful stage, on explicit force (first/get_status), else delta/skip.
    const bool needFull = forceFull || !_havePublishedBaseline;
    if (!needFull && !mqttStatusHasSignificantChanges(snapshot, _lastPublished, cfg)) {
        return true; // unchanged — no TX
    }

    PublishStatusCtx ctx{&snapshot, needFull ? nullptr : &_lastPublished, needFull};
    size_t measured = 0;
    const uint32_t t0 = millis();
    if (!_fsm.publishPrintedMeasured(_topicStatus, encodeMqttStatusForPublish, &ctx,
                                     JsonBytes::Mqtt::STATUS_PAYLOAD_MAX_BYTES, false, false, &measured)) {
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

    _lastPublished = snapshot;
    _havePublishedBaseline = true;
    logger.log("[MQTTClient] pub status %s bytes=%u\n", needFull ? "full" : "delta", (unsigned)measured);
    return true;
}
