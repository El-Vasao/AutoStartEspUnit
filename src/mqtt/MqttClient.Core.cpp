#include "mqtt/MqttClient.h"

#include "app/AppPorts.h"
#include "config/Config.h"
#include "config/SensorRomResolve.h"
#include "mqtt/HwMapJson.h"
#include "mqtt/MqttDebug.h"
#include "mqtt/MqttStatusBuilder.h"

#include "app/StatusSnapshot.h"
#include <WiFi.h>
#include <cstring>
#include "common/Constants.h"
#include "common/ErrorCodes.h"
#include "common/EspHal.h"
#include "common/Fnv1a.h"
#include "common/Logger.h"
#include "common/Pins.h"
#include "common/Utils.h"
#include "common/Version.h"
#include "core/Core.h"
#include "core/TimeSyncManager.h"
#include "gsm/GsmController.h"
#include "io/SensorsController.h"
#include "io/RelayController.h"
#include "io/DigitalInputs.h"
#include "program/ProgramExecutor.h"
#include "core/ErrorManager.h"
#include "mqtt/internal/CountingForwarder.h"

namespace {

struct PublishStatusCtx {
    const StatusSnapshot* cur;
    const StatusSnapshot* prev; ///< не nullptr, когда пишется дельта
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

static size_t encodeProgramsTopic(Print& p, void* /*ctx*/) {
    CountingForwarder fc(p);
    (void)config.emitProgramIndexArray(fc);
    return fc.written();
}

static size_t encodeHwMapTopic(Print& p, void* /*ctx*/) {
    CountingForwarder fc(p);
    emitHwMapObject(config.getBase(), fc);
    return fc.written();
}

static size_t encodeTrigMapTopic(Print& p, void* /*ctx*/) {
    CountingForwarder fc(p);
    emitTrigMapObject(config.getBase(), fc);
    return fc.written();
}

struct HashHexCtx {
    uint32_t hash;
};

static size_t encodeHashHex(Print& p, void* ctx) {
    CountingForwarder fc(p);
    auto* c = reinterpret_cast<const HashHexCtx*>(ctx);
    writeFnv1a32Hex(fc, c->hash);
    return fc.written();
}

} // namespace

void MqttClient::captureStatusSnapshot_(StatusSnapshot& s, bool fullErrHistory) const {
    s = StatusSnapshot{};
    s.uptimeSec = core.getUptime();
    strlcpy(s.modeName, core.getModeName(), sizeof(s.modeName));
    s.freeHeap = espHalFreeHeap();

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
        s.tempSensors[i].id = resolveSensorConfigId(sensors.getSensorAddress(i), cfg);
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
    {
        const uint32_t remMs = pe.getTimerRemainingMs();
        s.timerRemainingSec = msToSecCeil(remMs);
    }

    s.thermostatRuntime = core.getThermostatRuntime();
    s.batterySaverRuntime = core.getBatterySaverRuntime();

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

    {
        const auto& ts = core.getTimeSync();
        s.timeSynced = ts.isSynced();
        s.timeStale = ts.isStale();
        s.epochUtc = s.timeSynced ? static_cast<uint32_t>(ts.epochUtc()) : 0;
        s.tzOffsetHours = ts.tzOffsetHours();
        strlcpy(s.timeSource, ts.lastSource(), sizeof(s.timeSource));
    }

    {
        auto& gsm = core.getGSM();
        s.csqRssi = gsm.getSignalQuality();
        s.csqBer = gsm.getSignalBer();
    }

    ErrorSnapshotEntry rows[ErrorHistory::CAPACITY]{};
    const uint8_t n = fullErrHistory ? err.copyRecent(rows, ErrorHistory::CAPACITY)
                                     : err.copyUndelivered(rows, ErrorHistory::CAPACITY);
    s.lastErrCount = n;
    for (uint8_t i = 0; i < n; i++) {
        s.lastErr[i].code = rows[i].code;
        s.lastErr[i].active = rows[i].active;
        s.lastErr[i].uptimeSec = rows[i].uptimeSec;
        strlcpy(s.lastErr[i].msg, rows[i].msg, sizeof(s.lastErr[i].msg));
    }
}

static constexpr uint32_t kMqttPublishBudgetMs = NetTiming::MQTT_PUBLISH_BUDGET_MS;

MqttClient::MqttClient(Client& client)
    : _netClient(client), _lastStatusPublish(0), _lastReconnectAttempt(0), _fsm(client)
{
    _fsm.setPublishCallback(&MqttClient::onPublishThunk, this);
    _fsm.setTeleDequeuedCallback(&MqttClient::onTeleDequeuedThunk, this);
}

uint8_t MqttClient::getConsecutiveConnectFails() const {
    return _connectFailStreak;
}

const char* MqttClient::getLastConnectFailReason() const {
    return _fsm.lastErrorReason();
}

void MqttClient::joinTopic_(char* out, size_t outSz, const char* prefix, const char* suffix) {
    if (!out || outSz == 0) return;
    out[0] = '\0';
    if (!prefix || !prefix[0] || !suffix || !suffix[0]) return;
    snprintf(out, outSz, "%s/%s", prefix, suffix);
}

void MqttClient::rebuildTopics_(const char* prefix) {
    joinTopic_(_topicOnline, sizeof(_topicOnline), prefix, MqttTopics::ONLINE);
    joinTopic_(_topicState, sizeof(_topicState), prefix, MqttTopics::STATE);
    joinTopic_(_topicPrograms, sizeof(_topicPrograms), prefix, MqttTopics::PROGRAMS);
    joinTopic_(_topicHwMap, sizeof(_topicHwMap), prefix, MqttTopics::HW_MAP);
    joinTopic_(_topicTrigMap, sizeof(_topicTrigMap), prefix, MqttTopics::TRIG_MAP);
    joinTopic_(_topicCmd, sizeof(_topicCmd), prefix, MqttTopics::CMD);
    joinTopic_(_topicReply, sizeof(_topicReply), prefix, MqttTopics::REPLY);
#if defined(AUTOSTART_MQTT_DEBUG)
    joinTopic_(_topicCfg, sizeof(_topicCfg), prefix, MqttTopics::CFG);
    joinTopic_(_topicPrg, sizeof(_topicPrg), prefix, MqttTopics::PRG);
#endif
}

void MqttClient::begin() {
    const auto& mqttCfg = config.getBase().mqtt;

    const char* clientId = mqttCfg.client_id;
    if (!clientId || clientId[0] == '\0') {
        snprintf(_mqttClientId, sizeof(_mqttClientId), "autostart-%06X", (unsigned)espHalChipId());
    } else {
        strlcpy(_mqttClientId, clientId, sizeof(_mqttClientId));
    }
    strlcpy(_mqttUser, mqttCfg.user, sizeof(_mqttUser));
    strlcpy(_mqttPass, mqttCfg.pass, sizeof(_mqttPass));

    if (_bornPrefix[0] && strcmp(_bornPrefix, mqttCfg.topic_prefix) != 0) {
        _prefixTombstoneDue = true;
    }
    rebuildTopics_(mqttCfg.topic_prefix);

    MqttFsmClient::Config c{};
    c.host = mqttCfg.broker;
    c.port = mqttCfg.port;
    c.proto = MqttFsmClient::Proto::Mqtt311;
    c.clientId = _mqttClientId;
    c.username = (_mqttUser[0] ? _mqttUser : nullptr);
    c.password = (_mqttPass[0] ? _mqttPass : nullptr);
    static constexpr char kFalse[] = "false";
    if (_topicOnline[0]) {
        c.willTopic = _topicOnline;
        c.willMsg = kFalse;
        c.willRetain = true;
        c.willQos = 1;
    } else {
        c.willTopic = nullptr;
        c.willMsg = nullptr;
        c.willRetain = false;
        c.willQos = 0;
    }
    c.keepAliveSec = NetTiming::MQTT_KEEPALIVE_SEC;
    c.cleanSession = true;
    _fsm.begin(c);

    logger.log(LogModule::MqttClient, "start keepAlive=%us proto=%s idLen=%u userLen=%u passLen=%u online=%s state=%s cmd=%s reply=%s\n",
               (unsigned)NetTiming::MQTT_KEEPALIVE_SEC,
               "3.1.1",
               (unsigned)strlen(_mqttClientId),
               (unsigned)strlen(_mqttUser),
               (unsigned)strlen(_mqttPass),
               _topicOnline, _topicState, _topicCmd, _topicReply);
    logger.logSerialOnly(LogModule::MqttClient, "config: user=%s prefix=%s\n", _mqttUser, mqttCfg.topic_prefix);

    if (!_reconnectEnabled) {
        logger.log(LogModule::MqttClient, "begin(): reconnect off, first connect skipped\n");
    } else {
        _lastReconnectAttempt = millis();
        connect();
    }
}

void MqttClient::onTransportRxOverflow() {
    logger.log(LogModule::MqttClient, "transport RX overflow — forced reconnect\n");
    _fsm.forceError("rx_overflow");
}

void MqttClient::resetSessionLocal_() {
#if defined(AUTOSTART_MQTT_DEBUG)
    mqttDebugSession.abort();
#endif
    _subscribeQueued = false;
    _mqttWasConnected = false;
    _subWaitStartMs = 0;
    _subackTimeoutLogged = false;
    _birth = BirthPhase::WaitSuback;
    _havePublishedBaseline = false;
    _lastEdgeProbe = 0;
    _haveSeq = false;
    _statusSeq = 0;
    _haveQueuedStateSeq = false;
    _queuedStateSeq = 0;
    _queuedStateIsFull = false;
    _haveQueuedEncoded = false;
    _haveDeliveredSeq = false;
    _deliveredSeq = 0;
    clearPending_();
    clearInbound_();
    clearIdRing_();
    _hasActiveRun = false;
    _activeRunId[0] = '\0';
    _activeRunProgram = 0;
    _hasPreemptedRun = false;
    _preemptedRunId[0] = '\0';
    _preemptedRunProgram = 0;
    _cmdBusy = false;
}

void MqttClient::onTeleDequeuedThunk(void* ctx, MqttFsmClient::TeleKind kind) {
    if (!ctx) return;
    static_cast<MqttClient*>(ctx)->onTeleDequeued_(kind);
}

void MqttClient::onTeleDequeued_(MqttFsmClient::TeleKind kind) {
    // База сдвигается только после выхода кодированного кадра из очереди TX (CIPSEND подготовлен).
    // О SEND OK модема судят по позднему confirmLastErrDelivery_ (очередь пуста и !tcpBusy).
    if (kind == MqttFsmClient::TeleKind::Full || kind == MqttFsmClient::TeleKind::Delta) {
        if (_haveQueuedEncoded) {
            _deliveredSeq = _queuedEncoded.seq;
            _haveDeliveredSeq = true;
            _lastPublished = _queuedEncoded;
            _lastPublished.lastErrCount = 0;
            _havePublishedBaseline = true;
            _haveQueuedEncoded = false;
        }
        _haveQueuedStateSeq = false;
        _queuedStateIsFull = false;
    }
}

void MqttClient::loop() {
    if (!_reconnectEnabled && !_fsm.isDisconnectPending() && !_fsm.isConnected() &&
        _fsm.state() != MqttFsmClient::State::TcpConnecting &&
        _fsm.state() != MqttFsmClient::State::MqttConnecting) {
        return;
    }

    const bool modemCtrlBusy = _ctrlPlaneBusy && _ctrlPlaneBusy(_ctrlPlaneBusyCtx);

    const uint32_t now = millis();
    if (_reconnectEnabled && !_fsm.isConnected() && !modemCtrlBusy &&
        (now - _lastReconnectAttempt > NetTiming::MQTT_RECONNECT_INTERVAL_MS)) {
        if (_fsm.state() == MqttFsmClient::State::Idle || _fsm.state() == MqttFsmClient::State::Error) {
            _lastReconnectAttempt = now;
            connect();
        }
    }

    MqttFsmClient::Budgets b{};
    b.maxReadBytesPerTick = NetTiming::MQTT_TICK_MAX_READ_BYTES;
    b.maxWriteBytesPerTick = NetTiming::MQTT_TICK_MAX_WRITE_BYTES;
    b.maxParseFramesPerTick = NetTiming::MQTT_TICK_MAX_PARSE_FRAMES;
    b.maxMsPerTick = NetTiming::MQTT_TICK_MAX_MS;
    b.shouldBlockConnect = _ctrlPlaneBusy;
    b.shouldBlockConnectCtx = _ctrlPlaneBusyCtx;
    b.isTransportSendBusy = _transportSendBusy;
    b.isTransportSendBusyCtx = _transportSendBusyCtx;
    b.onRxIncomplete = _onRxIncomplete;
    b.onRxIncompleteCtx = _onRxIncompleteCtx;
    _fsm.tick(b);

    noteSessionErrorIfNeeded_();

    // Слив: только качать FSM (online=false → DISCONNECT → stop). Без SUBSCRIBE/birth/steady.
    if (_fsm.isDisconnectPending()) return;

    if (_fsm.state() == MqttFsmClient::State::Error || !_fsm.isConnected()) {
        abandonLastErrDelivery_();
    } else {
        confirmLastErrDelivery_();
    }

    if (_fsm.isConnected()) {
        if (!_subscribeQueued && _topicCmd[0]) {
            if (_fsm.subscribe(_topicCmd)) {
                _subscribeQueued = true;
                _subWaitStartMs = millis();
                _subackTimeoutLogged = false;
                logger.log(LogModule::MqttClient, "connected. subscription=%s state=%s online=%s\n",
                           _topicCmd, _topicState, _topicOnline);
            }
        }

        if (!_mqttWasConnected) {
            _mqttWasConnected = true;
            _connectFailStreak = 0;
            _sessionErrorRecorded = false;
            _lastStatusPublish = 0;
            _lastEdgeProbe = 0;
            _havePublishedBaseline = false;
            _haveSeq = false;
            _statusSeq = 0;
            _haveDeliveredSeq = false;
            _deliveredSeq = 0;
            _haveQueuedStateSeq = false;
            _haveQueuedEncoded = false;
            _birth = BirthPhase::WaitSuback;
            _programsCatalogDirty = true;
            _hwCatalogDirty = true;
            _trigCatalogDirty = true;
            core.getErrorManager().clear(ErrorCode::MQTT_CONNECT_FAIL);
        }

        static constexpr uint32_t kSubackTimeoutMs = NetTiming::MQTT_SUBACK_TIMEOUT_MS;
        const bool subConfirmed = _fsm.isSubscribeConfirmed();
        const bool subPending = _topicCmd[0] && _fsm.isSubscribePending();
        const bool subTimedOut =
            subPending && !subConfirmed && _subWaitStartMs != 0 &&
            (millis() - _subWaitStartMs) >= kSubackTimeoutMs;
        if (subTimedOut) {
            if (!_subackTimeoutLogged) {
                _subackTimeoutLogged = true;
                logger.log(LogModule::MqttClient, "SUBACK timeout %ums — retrying SUBSCRIBE\n",
                           (unsigned)kSubackTimeoutMs);
            }
            _fsm.requeueSubscribe();
            _subWaitStartMs = millis();
        }
        const bool subReady = !_topicCmd[0] || subConfirmed;
        const bool txOk = !modemCtrlBusy;

        if (txOk) {
            flushPending_();
            drainInbound_();
#if defined(AUTOSTART_MQTT_DEBUG)
            mqttDebugSession.tickDownlink(*this);
#endif
        }

        const bool teleIdle = (_inboundCount == 0) && (_pendingCount == 0) && !_cmdBusy && !_fsm.hasCtrlOutbound();

        if (txOk && subReady) {
            // Один раз: снять брошенный retain префикса до публикаций нового birth.
            if (_prefixTombstoneDue && _bornPrefix[0] && teleIdle && !_fsm.hasOutbound()) {
                char t[TextBytes::Mqtt::TOPIC];
                static const char* kSuf[] = {
                    MqttTopics::ONLINE, MqttTopics::STATE, MqttTopics::PROGRAMS,
                    MqttTopics::HW_MAP, MqttTopics::TRIG_MAP
                };
                bool allOk = true;
                for (const char* suf : kSuf) {
                    joinTopic_(t, sizeof(t), _bornPrefix, suf);
                    if (!publishEmptyRetain_(t)) {
                        allOk = false;
                        break;
                    }
                }
                if (allOk) {
                    _bornPrefix[0] = '\0';
                    _prefixTombstoneDue = false;
                }
            } else if (_birth == BirthPhase::Steady) {
                tickSteady_(txOk, subReady, teleIdle);
            } else {
                tickBirth_(txOk, subReady, teleIdle);
            }
        }
    } else if (!_fsm.isDisconnectPending()) {
        resetSessionLocal_();
    }
}

void MqttClient::tickBirth_(bool txOk, bool subReady, bool teleIdle) {
    (void)txOk;
    (void)subReady;
    if (!teleIdle) return;
    if (_fsm.hasOutbound()) return; // ждать опустошения эпохи SEND по пустой очереди

    if (_birth == BirthPhase::WaitSuback) {
        _birth = BirthPhase::CatalogPrograms;
    }

    if (_birth == BirthPhase::CatalogPrograms) {
        if (!_programsCatalogDirty) {
            _birth = BirthPhase::CatalogHwMap;
        } else if (publishProgramsHash_()) {
            _birth = BirthPhase::CatalogHwMap;
            return;
        } else {
            // Пропустить неудачный каталог; birth продолжается; повтор позже в Steady.
            _birth = BirthPhase::CatalogHwMap;
        }
    }
    if (_birth == BirthPhase::CatalogHwMap) {
        if (!_hwCatalogDirty) {
            _birth = BirthPhase::CatalogTrigMap;
        } else if (publishHwMapHash_()) {
            _birth = BirthPhase::CatalogTrigMap;
            return;
        } else {
            _birth = BirthPhase::CatalogTrigMap;
        }
    }
    if (_birth == BirthPhase::CatalogTrigMap) {
        if (!_trigCatalogDirty) {
            _birth = BirthPhase::FullState;
        } else if (publishTrigMapHash_()) {
            _birth = BirthPhase::FullState;
            return;
        } else {
            _birth = BirthPhase::FullState;
        }
    }
    if (_birth == BirthPhase::FullState) {
        if (_fsm.hasFullTeleOutbound()) return;
        if (publishStatus(true)) {
            _lastStatusPublish = millis();
            _lastEdgeProbe = _lastStatusPublish;
            _birth = BirthPhase::OnlineTrue;
            return;
        }
        return;
    }
    if (_birth == BirthPhase::OnlineTrue) {
        // Ждать, пока full выйдет из очереди (SEND подготовлен), и только потом online=true.
        if (_fsm.hasFullTeleOutbound() || _fsm.hasOutbound()) return;
        if (publishOnline_(true)) {
            _birth = BirthPhase::Steady;
            strlcpy(_bornPrefix, config.getBase().mqtt.topic_prefix, sizeof(_bornPrefix));
            core.cooperate();
        }
    }
}

void MqttClient::tickSteady_(bool txOk, bool subReady, bool teleIdle) {
    (void)txOk;
    (void)subReady;
    const uint32_t now = millis();

    // В тихие промежутки помечать хеши каталога грязными (слот retain = 8 hex, не тело).
    if (teleIdle && !_fsm.hasOutbound()) {
        if (_programsCatalogDirty) {
            if (publishProgramsHash_()) return;
        } else if (_hwCatalogDirty) {
            if (publishHwMapHash_()) return;
        } else if (_trigCatalogDirty) {
            if (publishTrigMapHash_()) return;
        }
    }

    if (!teleIdle) return;
    if (_fsm.hasFullTeleOutbound()) return; // нет дельты, пока full ждёт отправки

    // Пометить каталоги грязными, когда хеш конфига ушёл дальше опубликованного.
    {
        const auto& base = config.getBase();
        if (config.programsRev() != _publishedProgramsRev) _programsCatalogDirty = true;
        if (hwMapObjectFnv(base) != _publishedHwMapRev) _hwCatalogDirty = true;
        if (trigMapObjectFnv(base) != _publishedTrigMapRev) _trigCatalogDirty = true;
    }

    const auto& mqttCfg = config.getBase().mqtt;
    const uint32_t interval_ms = secToMs(mqttCfg.publish_interval_sec);
    const bool intervalDue =
        (_lastStatusPublish == 0) || (now - _lastStatusPublish >= interval_ms);
    const bool edgeDue = _havePublishedBaseline &&
                         (now - _lastEdgeProbe >= NetTiming::MQTT_EDGE_STATUS_GAP_MS);

    if (intervalDue) {
        if (publishStatus(true)) {
            _lastStatusPublish = now;
            _lastEdgeProbe = now;
            core.cooperate();
        }
    } else if (edgeDue) {
        _lastEdgeProbe = now;
        if (tryPublishEdgeStatus_()) {
            core.cooperate();
        }
    }
}

bool MqttClient::publishEmptyRetain_(const char* topic) {
    if (!topic || !topic[0]) return false;
    static constexpr uint8_t kEmpty[] = {0};
    return _fsm.publish(topic, kEmpty, 0, true, true);
}

bool MqttClient::publishOnline_(bool on) {
    if (!_topicOnline[0]) return false;
    static constexpr uint8_t kTrue[] = {'t', 'r', 'u', 'e'};
    static constexpr uint8_t kFalse[] = {'f', 'a', 'l', 's', 'e'};
    const uint8_t* payload = on ? kTrue : kFalse;
    const uint16_t len = on ? 4u : 5u;
    if (!_fsm.publish(_topicOnline, payload, len, true, true)) return false;
    logger.log(LogModule::MqttClient, "pub online %s\n", on ? "true" : "false");
    return true;
}

bool MqttClient::publishProgramsHash_() {
    if (!_topicPrograms[0]) return false;
    HashHexCtx ctx{config.programsRev()};
    size_t measured = 0;
    if (!_fsm.publishPrintedMeasured(_topicPrograms, encodeHashHex, &ctx, 8, true, false, &measured,
                                     MqttFsmClient::TeleKind::Catalog, 0)) {
        return false;
    }
    _programsCatalogDirty = false;
    _publishedProgramsRev = ctx.hash;
    logger.log(LogModule::MqttClient, "pub programs hash bytes=%u\n", (unsigned)measured);
    return true;
}

bool MqttClient::publishHwMapHash_() {
    if (!_topicHwMap[0]) return false;
    HashHexCtx ctx{hwMapObjectFnv(config.getBase())};
    size_t measured = 0;
    if (!_fsm.publishPrintedMeasured(_topicHwMap, encodeHashHex, &ctx, 8, true, false, &measured,
                                     MqttFsmClient::TeleKind::Catalog, 0)) {
        return false;
    }
    _hwCatalogDirty = false;
    _publishedHwMapRev = ctx.hash;
    logger.log(LogModule::MqttClient, "pub hwMap hash bytes=%u\n", (unsigned)measured);
    return true;
}

bool MqttClient::publishTrigMapHash_() {
    if (!_topicTrigMap[0]) return false;
    HashHexCtx ctx{trigMapObjectFnv(config.getBase())};
    size_t measured = 0;
    if (!_fsm.publishPrintedMeasured(_topicTrigMap, encodeHashHex, &ctx, 8, true, false, &measured,
                                     MqttFsmClient::TeleKind::Catalog, 0)) {
        return false;
    }
    _trigCatalogDirty = false;
    _publishedTrigMapRev = ctx.hash;
    logger.log(LogModule::MqttClient, "pub trigMap hash bytes=%u\n", (unsigned)measured);
    return true;
}

bool MqttClient::publishProgramsBody_() {
    if (!_topicPrograms[0]) return false;
    size_t measured = 0;
    if (!_fsm.publishPrintedMeasured(_topicPrograms, encodeProgramsTopic, nullptr,
                                     JsonBytes::Mqtt::PROGRAMS_TOPIC_JSON_MAX, false, false, &measured,
                                     MqttFsmClient::TeleKind::Catalog, 0)) {
        return false;
    }
    // Тело без retain: слот хеша у брокера не обновлять; dirty снимает только publish*Hash_.
    logger.log(LogModule::MqttClient, "pub programs body bytes=%u\n", (unsigned)measured);
    return true;
}

bool MqttClient::publishHwMapBody_() {
    if (!_topicHwMap[0]) return false;
    size_t measured = 0;
    if (!_fsm.publishPrintedMeasured(_topicHwMap, encodeHwMapTopic, nullptr,
                                     JsonBytes::Mqtt::HWMAP_TOPIC_JSON_MAX, false, false, &measured,
                                     MqttFsmClient::TeleKind::Catalog, 0)) {
        return false;
    }
    logger.log(LogModule::MqttClient, "pub hwMap body bytes=%u\n", (unsigned)measured);
    return true;
}

bool MqttClient::publishTrigMapBody_() {
    if (!_topicTrigMap[0]) return false;
    size_t measured = 0;
    if (!_fsm.publishPrintedMeasured(_topicTrigMap, encodeTrigMapTopic, nullptr,
                                     JsonBytes::Mqtt::TRIGMAP_TOPIC_JSON_MAX, false, false, &measured,
                                     MqttFsmClient::TeleKind::Catalog, 0)) {
        return false;
    }
    logger.log(LogModule::MqttClient, "pub trigMap body bytes=%u\n", (unsigned)measured);
    return true;
}

void MqttClient::failQueuedInboundOnDisconnect_() {
    while (_inboundCount > 0) {
        MqttCommand cmd = _inbound[_inboundHead];
        _inbound[_inboundHead] = MqttCommand{};
        _inboundHead = (uint8_t)((_inboundHead + 1u) % MqttCmd::INBOUND_DEPTH);
        _inboundCount--;
        if (!cmd.id[0]) continue;
        PendingReply pr{};
        pr.kind = PendingKind::Thin;
        strlcpy(pr.id, cmd.id, sizeof(pr.id));
        pr.code = MqttCmd::CODE_INTERNAL;
        (void)stageReply_(pr);
        finalizeIdRing_(cmd.id, MqttCmd::CODE_INTERNAL);
        logger.log(LogModule::MqttClient, "disconnect: fail queued id=%s\n", cmd.id);
    }
}

void MqttClient::disconnect() {
    // Повторный слив (голос/SMS, time-sync) не должен снова класть online=false в очередь —
    // иначе CIPSEND не опустеет и DISCONNECT/AT+CMGS не стартуют.
    if (_fsm.isDisconnectPending()) return;
    logger.log(LogModule::MqttClient, "calling disconnect()\n");
    abandonLastErrDelivery_();
    // Пока сессия ещё Connected — ответить 500 на каждую команду с 202 в FIFO.
    failQueuedInboundOnDisconnect_();
    if (_fsm.isConnected() && _topicOnline[0]) {
        (void)publishOnline_(false);
    }
    _fsm.requestDisconnect();
    resetSessionLocal_();
}

void MqttClient::abortSessionHard(const char* reason) {
    setReconnectEnabled(false);
    abandonLastErrDelivery_();
    clearInbound_();
    clearPending_();
    _fsm.abortLocal(reason ? reason : "hard_drop");
    resetSessionLocal_();
}

bool MqttClient::needsDisconnectDrain() const {
    return _fsm.isDisconnectPending() || _fsm.hasOutbound();
}

void MqttClient::noteSessionErrorIfNeeded_() {
    if (_fsm.state() != MqttFsmClient::State::Error) return;
    if (_sessionErrorRecorded) return;
    _sessionErrorRecorded = true;
    if (_connectFailStreak < 255) _connectFailStreak++;
    const char* why = _fsm.lastErrorReason();
    if (why && why[0]) {
        logger.log(LogModule::MqttClient, "session error=%s series=%u\n", why, (unsigned)_connectFailStreak);
    }
    core.getErrorManager().set(ErrorCode::MQTT_CONNECT_FAIL);
}

void MqttClient::connect() {
    if (_ctrlPlaneBusy && _ctrlPlaneBusy(_ctrlPlaneBusyCtx)) {
        return;
    }
    noteSessionErrorIfNeeded_();
    const auto& mqttCfg = config.getBase().mqtt;
    if (_fsm.state() == MqttFsmClient::State::Idle ||
        _fsm.state() == MqttFsmClient::State::Error) {
        logger.log(LogModule::MqttClient, "connect() broker=%s:%u\n", mqttCfg.broker, (unsigned)mqttCfg.port);
    }
    _fsm.requestConnect();
}

bool MqttClient::publishStatus(bool forceFull) {
    if (!_topicState[0]) return false;

    const bool needFull = forceFull || !_havePublishedBaseline;
    StatusSnapshot snapshot{};
    captureStatusSnapshot_(snapshot, needFull);

    // markDelivered только по undelivered; full history не должна повторно ждать confirm.
    ErrorSnapshotEntry undeliv[ErrorHistory::CAPACITY]{};
    const uint8_t undelivN = core.getErrorManager().copyUndelivered(undeliv, ErrorHistory::CAPACITY);

    if (_lastErrAwaitConfirm && !needFull) {
        snapshot.lastErrCount = 0;
    }

    if (!needFull) {
        // Путь фронта идёт через tryPublishEdgeStatus_, проверка уже была; пропуск оставлен для надёжности.
        if (snapshot.lastErrCount == 0 &&
            !mqttStatusHasSignificantChanges(snapshot, _lastPublished, config.getBase())) {
            return false;
        }
    }

    // Назначить seq: при замене неотправленного Tele взять seq из очереди, иначе выдать следующий.
    uint32_t seq;
    if (_haveQueuedStateSeq && (_queuedStateIsFull || !needFull)) {
        seq = _queuedStateSeq;
    } else if (_haveQueuedStateSeq && needFull) {
        // Full заменяет ждущую дельту — оставить тот же ещё не израсходованный seq.
        seq = _queuedStateSeq;
    } else {
        seq = _haveSeq ? (_statusSeq + 1u) : 1u;
        if (seq == 0) seq = 1; // перескок через 0
    }
    snapshot.seq = seq;

    PublishStatusCtx ctx{&snapshot, needFull ? nullptr : &_lastPublished, needFull};
    size_t measured = 0;
    const uint32_t t0 = millis();
    const MqttFsmClient::TeleKind kind =
        needFull ? MqttFsmClient::TeleKind::Full : MqttFsmClient::TeleKind::Delta;
    if (!_fsm.publishPrintedMeasured(_topicState, encodeMqttStatusForPublish, &ctx,
                                     JsonBytes::Mqtt::STATUS_PAYLOAD_MAX_BYTES, needFull /*retain*/, false,
                                     &measured, kind, 0)) {
        if (measured > JsonBytes::Mqtt::STATUS_PAYLOAD_MAX_BYTES) {
            logger.log(LogModule::MqttClient, "publishStatus: JSON too large (max %u, got %u)\n",
                       (unsigned)JsonBytes::Mqtt::STATUS_PAYLOAD_MAX_BYTES, (unsigned)measured);
        }
        return false;
    }
    const uint32_t dt = millis() - t0;
    if (dt > kMqttPublishBudgetMs) {
        logger.log(LogModule::MqttClient, "status publish slow (%u ms)\n", (unsigned)dt);
    }

    _statusSeq = seq;
    _haveSeq = true;
    _queuedStateSeq = seq;
    _haveQueuedStateSeq = true;
    _queuedStateIsFull = needFull;
    _queuedEncoded = snapshot;
    _haveQueuedEncoded = true;

    if (!_lastErrAwaitConfirm && undelivN > 0) {
        _lastErrPendingCount = undelivN;
        for (uint8_t i = 0; i < undelivN && i < ErrorHistory::CAPACITY; i++) {
            _lastErrPending[i].code = undeliv[i].code;
            _lastErrPending[i].active = undeliv[i].active;
            _lastErrPending[i].uptimeSec = undeliv[i].uptimeSec;
            strlcpy(_lastErrPending[i].msg, undeliv[i].msg, sizeof(_lastErrPending[i].msg));
        }
        _lastErrAwaitConfirm = true;
    }

    // Здесь _lastPublished не двигать — ждать снятия Tele с очереди (путь SEND).
    logger.log(LogModule::MqttClient, "pub state %s seq=%lu bytes=%u\n", needFull ? "full" : "delta",
               (unsigned long)seq, (unsigned)measured);
    return true;
}

bool MqttClient::tryPublishEdgeStatus_() {
    if (!_topicState[0] || !_havePublishedBaseline) return false;
    if (_fsm.hasFullTeleOutbound()) return false;
    StatusSnapshot snapshot{};
    captureStatusSnapshot_(snapshot, false);
    if (_lastErrAwaitConfirm) snapshot.lastErrCount = 0;

    if (snapshot.lastErrCount == 0 &&
        !mqttStatusHasEdgeChanges(snapshot, _lastPublished, config.getBase())) {
        return false;
    }
    return publishStatus(false);
}

void MqttClient::confirmLastErrDelivery_() {
    if (!_lastErrAwaitConfirm) return;
    if (_fsm.hasOutbound()) return;
    if (_ctrlPlaneBusy && _ctrlPlaneBusy(_ctrlPlaneBusyCtx)) return;
    core.getErrorManager().markDelivered(_lastErrPending, _lastErrPendingCount);
    _lastErrAwaitConfirm = false;
    _lastErrPendingCount = 0;
}

void MqttClient::abandonLastErrDelivery_() {
    if (!_lastErrAwaitConfirm) return;
    logger.log(LogModule::MqttClient, "last_err delivery dropped (session/tcp) — will retry\n");
    _lastErrAwaitConfirm = false;
    _lastErrPendingCount = 0;
}
