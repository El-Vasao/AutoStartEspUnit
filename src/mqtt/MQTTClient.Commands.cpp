#include "mqtt/MQTTClient.h"

#include "app/AppPorts.h"
#include "config/Config.h"
#include "mqtt/MqttCommandParser.h"
#include "mqtt/MqttStatusBuilder.h"

#include "common/Constants.h"
#include "common/Logger.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "io/DigitalInputs.h"
#include "io/RelayController.h"
#include "io/SensorsController.h"
#include "program/ProgramExecutor.h"
#include "common/Pins.h"

#include <cstring>

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
};

struct ThinReplyCtx {
    const char* id;
    uint16_t code;
};

size_t encodeThinReply(Print& p, void* ctx) {
    auto* c = reinterpret_cast<const ThinReplyCtx*>(ctx);
    CountingForwarder fc(p);
    fc.print("{\"id\":\"");
    fc.print(c->id ? c->id : "");
    fc.print("\",\"code\":");
    fc.print(c->code);
    fc.print('}');
    return fc.n;
}

struct ListReplyCtx {
    const char* id;
};

size_t encodeListReply(Print& p, void* ctx) {
    auto* c = reinterpret_cast<const ListReplyCtx*>(ctx);
    CountingForwarder fc(p);
    fc.print("{\"id\":\"");
    fc.print(c->id ? c->id : "");
    fc.print("\",\"code\":");
    fc.print(MqttCmd::CODE_OK);
    fc.print(",\"programs\":");
    (void)config.emitProgramIndexArray(fc);
    fc.print('}');
    return fc.n;
}

struct StatusReplyCtx {
    const char* id;
    const StatusSnapshot* snap;
};

size_t encodeStatusReply(Print& p, void* ctx) {
    auto* c = reinterpret_cast<const StatusReplyCtx*>(ctx);
    CountingForwarder fc(p);
    fc.print("{\"id\":\"");
    fc.print(c->id ? c->id : "");
    fc.print("\",\"code\":");
    fc.print(MqttCmd::CODE_OK);
    fc.print(",\"status\":");
    if (c->snap) {
        emitMqttStatusJson(*c->snap, config.getBase(), fc);
    } else {
        fc.print("null");
    }
    fc.print('}');
    return fc.n;
}

} // namespace

void MQTTClient::onProgramLifecycleThunk(void* ctx, uint8_t programId, bool finishedOk) {
    if (!ctx) return;
    static_cast<MQTTClient*>(ctx)->onProgramLifecycle(programId, finishedOk);
}

void MQTTClient::onProgramLifecycle(uint8_t programId, bool finishedOk) {
    if (!_hasActiveRun || _activeRunProgram != programId || _activeRunId[0] == '\0') {
        return;
    }
    const uint16_t code = finishedOk ? MqttCmd::CODE_OK : MqttCmd::CODE_INTERNAL;
    PendingReply pr{};
    pr.kind = PendingKind::Thin;
    strlcpy(pr.id, _activeRunId, sizeof(pr.id));
    pr.code = code;
    logger.log("[MQTTClient] cmd final id=%s code=%u\n", pr.id, (unsigned)code);
    (void)stageReply_(pr);
    _hasActiveRun = false;
    _activeRunId[0] = '\0';
    _activeRunProgram = 0;
}

void MQTTClient::clearPending_() {
    for (uint8_t i = 0; i < MqttCmd::PENDING_REPLY_DEPTH; i++) {
        _pending[i] = PendingReply{};
    }
    _pendingCount = 0;
}

void MQTTClient::clearInbound_() {
    _inboundHead = 0;
    _inboundCount = 0;
    for (uint8_t i = 0; i < MqttCmd::INBOUND_DEPTH; i++) {
        _inbound[i] = MqttCommand{};
    }
}

bool MQTTClient::enqueueInbound_(const MqttCommand& cmd) {
    if (_inboundCount >= MqttCmd::INBOUND_DEPTH) return false;
    const uint8_t tail = (uint8_t)((_inboundHead + _inboundCount) % MqttCmd::INBOUND_DEPTH);
    _inbound[tail] = cmd;
    _inboundCount++;
    logger.log("[MQTTClient] cmd queued n=%u id=%s\n", (unsigned)_inboundCount, cmd.id);
    return true;
}

void MQTTClient::drainInbound_() {
    // Serialize Ctrl: wait until MQTT Ctrl queue AND modem CIPSEND are idle.
    // Otherwise final (esp. fat list/status) overlaps ack CIPSEND and desyncs RX — later cmds lose ack.
    while (_inboundCount > 0 && !_cmdBusy && !ctrlPlaneBlocked_()) {
        MqttCommand cmd = _inbound[_inboundHead];
        _inbound[_inboundHead] = MqttCommand{};
        _inboundHead = (uint8_t)((_inboundHead + 1u) % MqttCmd::INBOUND_DEPTH);
        _inboundCount--;
        logger.log("[MQTTClient] cmd dispatch kind=%s id=%s\n", mqttCmdKindStr(cmd.kind), cmd.id);
        _cmdBusy = true;
        dispatchCommand_(cmd);
        _cmdBusy = false;
        break;
    }
}

bool MQTTClient::enqueuePending_(const PendingReply& pr) {
    if (_pendingCount >= MqttCmd::PENDING_REPLY_DEPTH) return false;
    _pending[_pendingCount++] = pr;
    return true;
}

void MQTTClient::evictOldestPending_() {
    if (_pendingCount == 0) return;
    logger.log("[MQTTClient] reply pending evict id=%s code=%u\n", _pending[0].id, (unsigned)_pending[0].code);
    for (uint8_t i = 1; i < _pendingCount; i++) {
        _pending[i - 1] = _pending[i];
    }
    _pendingCount--;
    _pending[_pendingCount] = PendingReply{};
}

bool MQTTClient::publishThinReply_(const char* id, uint16_t code) {
    if (!_topicReply[0]) return false;
    ThinReplyCtx ctx{id, code};
    size_t measured = 0;
    if (!_fsm.publishPrintedMeasured(_topicReply, encodeThinReply, &ctx, 96, false, true, &measured)) {
        return false;
    }
    logger.log("[MQTTClient] pub reply id=%s code=%u bytes=%u\n", id ? id : "", (unsigned)code,
               (unsigned)measured);
    return true;
}

bool MQTTClient::publishListReply_(const char* id) {
    if (!_topicReply[0]) return false;
    ListReplyCtx ctx{id};
    size_t measured = 0;
    if (!_fsm.publishPrintedMeasured(_topicReply, encodeListReply, &ctx,
                                     JsonBytes::Mqtt::LIST_PROGRAMS_JSON_MAX, false, true, &measured)) {
        if (measured > JsonBytes::Mqtt::LIST_PROGRAMS_JSON_MAX) {
            logger.log("[MQTTClient] list reply too large measured=%u\n", (unsigned)measured);
            return publishThinReply_(id, MqttCmd::CODE_UNPROCESSABLE);
        }
        return false;
    }
    logger.log("[MQTTClient] pub reply list id=%s code=200 bytes=%u\n", id ? id : "", (unsigned)measured);
    return true;
}

bool MQTTClient::publishStatusReply_(const char* id) {
    if (!_topicReply[0]) return false;
    StatusSnapshot snap{};
    captureStatusSnapshot_(snap);
    StatusReplyCtx ctx{id, &snap};
    size_t measured = 0;
    if (!_fsm.publishPrintedMeasured(_topicReply, encodeStatusReply, &ctx,
                                     JsonBytes::Mqtt::STATUS_REPLY_JSON_MAX, false, true, &measured)) {
        if (measured > JsonBytes::Mqtt::STATUS_REPLY_JSON_MAX) {
            logger.log("[MQTTClient] status reply too large measured=%u\n", (unsigned)measured);
            return publishThinReply_(id, MqttCmd::CODE_UNPROCESSABLE);
        }
        return false;
    }
    logger.log("[MQTTClient] pub reply status id=%s code=200 bytes=%u\n", id ? id : "", (unsigned)measured);
    return true;
}

bool MQTTClient::ctrlPlaneBlocked_() const {
    if (_fsm.hasCtrlOutbound()) return true;
    if (_ctrlPlaneBusy && _ctrlPlaneBusy(_ctrlPlaneBusyCtx)) return true;
    return false;
}

bool MQTTClient::stageReply_(const PendingReply& pr) {
    // Serialize Ctrl TX across MQTT queue + modem CIPSEND epoch.
    if (ctrlPlaneBlocked_()) {
        if (enqueuePending_(pr)) {
            logger.log("[MQTTClient] reply wait Ctrl id=%s code=%u kind=%u\n", pr.id, (unsigned)pr.code,
                       (unsigned)pr.kind);
            return true;
        }
        evictOldestPending_();
        if (enqueuePending_(pr)) {
            logger.log("[MQTTClient] reply wait Ctrl after evict id=%s code=%u\n", pr.id, (unsigned)pr.code);
            return true;
        }
        logger.log("[MQTTClient] reply drop id=%s code=%u\n", pr.id, (unsigned)pr.code);
        return false;
    }

    bool ok = false;
    if (pr.kind == PendingKind::Thin) {
        ok = publishThinReply_(pr.id, pr.code);
    } else if (pr.kind == PendingKind::List) {
        ok = publishListReply_(pr.id);
    } else if (pr.kind == PendingKind::Status) {
        ok = publishStatusReply_(pr.id);
    }

    if (ok) return true;

    if (enqueuePending_(pr)) {
        logger.log("[MQTTClient] reply stage queued kind=%u id=%s code=%u\n", (unsigned)pr.kind, pr.id,
                   (unsigned)pr.code);
        return true;
    }
    evictOldestPending_();
    if (enqueuePending_(pr)) {
        logger.log("[MQTTClient] reply stage queued after evict id=%s code=%u\n", pr.id, (unsigned)pr.code);
        return true;
    }
    logger.log("[MQTTClient] reply drop id=%s code=%u\n", pr.id, (unsigned)pr.code);
    return false;
}

void MQTTClient::flushPending_() {
    while (_pendingCount > 0) {
        if (ctrlPlaneBlocked_()) break;
        PendingReply& pr = _pending[0];
        bool ok = false;
        if (pr.kind == PendingKind::Thin) {
            ok = publishThinReply_(pr.id, pr.code);
        } else if (pr.kind == PendingKind::List) {
            ok = publishListReply_(pr.id);
        } else if (pr.kind == PendingKind::Status) {
            ok = publishStatusReply_(pr.id);
        }
        if (!ok) break;
        for (uint8_t i = 1; i < _pendingCount; i++) {
            _pending[i - 1] = _pending[i];
        }
        _pendingCount--;
        _pending[_pendingCount] = PendingReply{};
        break;
    }
}

bool MQTTClient::validateArgs_(const MqttCommand& cmd, MqttCmdErr& errOut) const {
    errOut = MqttCmdErr::None;
    if (cmd.id[0] == '\0' || strlen(cmd.id) > MqttCmd::ID_MAX_LEN) {
        errOut = MqttCmdErr::Args;
        return false;
    }
    switch (cmd.kind) {
        case MqttCommandKind::Run:
            if (cmd.programId == 0) {
                errOut = MqttCmdErr::Args;
                return false;
            }
            return true;
        case MqttCommandKind::Stop:
        case MqttCommandKind::List:
        case MqttCommandKind::Status:
            return true;
        case MqttCommandKind::Set:
            if (cmd.setName == MqttSetName::None || !cmd.hasEnabled) {
                errOut = MqttCmdErr::Args;
                return false;
            }
            if ((cmd.setName == MqttSetName::Input || cmd.setName == MqttSetName::Trigger ||
                 cmd.setName == MqttSetName::TempTrigger) &&
                cmd.ref == 0) {
                errOut = MqttCmdErr::Args;
                return false;
            }
            return true;
        default:
            errOut = MqttCmdErr::Unknown;
            return false;
    }
}

void MQTTClient::handlePublish(const char* topic, const uint8_t* payload, uint16_t length, bool retained) {
    (void)retained;
    if (!_topicCmd[0]) {
        logger.log("[MQTTClient] cmd drop: empty topicCmd\n");
        return;
    }
    if (!topic) {
        logger.log("[MQTTClient] cmd drop: null topic\n");
        return;
    }
    if (strcmp(topic, _topicCmd) != 0) {
        // Wrong topic: ignore without tearing the session (resync may deliver a false PUBLISH head).
        logger.log("[MQTTClient] ignore topic got=%s want=%s\n", topic, _topicCmd);
        return;
    }
    tryAckIngress_(payload, length);
}

void MQTTClient::tryAckIngress_(const uint8_t* payload, uint16_t length) {
    logger.log("[MQTTClient] cmd recv len=%u\n", (unsigned)length);

    char message[JsonBytes::Mqtt::CMD_JSON_MAX];
    if (length == 0 || length >= sizeof(message)) {
        logger.log("[MQTTClient] Command payload too large (%u)\n", (unsigned)length);
        return;
    }
    if (!payload) return;
    memcpy(message, payload, length);
    message[length] = '\0';

    auto stageAck = [this](const char* id, uint16_t code) {
        PendingReply pr{};
        pr.kind = PendingKind::Thin;
        strlcpy(pr.id, id, sizeof(pr.id));
        pr.code = code;
        logger.log("[MQTTClient] cmd ack id=%s code=%u\n", id, (unsigned)code);
        if (!stageReply_(pr)) {
            logger.log("[MQTTClient] cmd ack could not be staged id=%s code=%u\n", id, (unsigned)code);
        }
    };

    MqttCommand cmd;
    if (!parseMqttCommandJson(message, cmd)) {
        char idBuf[MqttCmd::ID_MAX_LEN + 1]{};
        (void)mqttExtractReqId(message, idBuf, sizeof(idBuf));
        logger.log("[MQTTClient] cmd unrecognized\n");
        if (idBuf[0]) stageAck(idBuf, MqttCmd::CODE_BAD_REQUEST);
        return;
    }

    MqttCmdErr argErr = MqttCmdErr::None;
    if (!validateArgs_(cmd, argErr)) {
        logger.log("[MQTTClient] cmd invalid id=%s\n", cmd.id);
        stageAck(cmd.id, mqttErrToHttpCode(argErr));
        return;
    }

    if (_inboundCount >= MqttCmd::INBOUND_DEPTH) {
        logger.log("[MQTTClient] cmd inbound full id=%s\n", cmd.id);
        stageAck(cmd.id, MqttCmd::CODE_UNAVAILABLE);
        return;
    }

    stageAck(cmd.id, MqttCmd::CODE_ACCEPTED);
    logger.log("[MQTTClient] cmd ack kind=%s id=%s\n", mqttCmdKindStr(cmd.kind), cmd.id);
    (void)enqueueInbound_(cmd);
}

void MQTTClient::dispatchCommand_(const MqttCommand& cmd) {
    switch (cmd.kind) {
        case MqttCommandKind::Run: {
            logger.log("[MQTTClient] run program=%u id=%s\n", (unsigned)cmd.programId, cmd.id);
            // Arm before startProgram: 1-step programs may finish() synchronously and
            // fire onProgramLifecycle before we return — otherwise final /reply is dropped.
            strlcpy(_activeRunId, cmd.id, sizeof(_activeRunId));
            _activeRunProgram = cmd.programId;
            _hasActiveRun = true;
            bool started = false;
            if (_ports && _ports->control.startProgram) {
                started = _ports->control.startProgram(_ports->control.ctx, cmd.programId);
            }
            if (!started) {
                _hasActiveRun = false;
                _activeRunId[0] = '\0';
                _activeRunProgram = 0;
                PendingReply pr{};
                pr.kind = PendingKind::Thin;
                strlcpy(pr.id, cmd.id, sizeof(pr.id));
                pr.code = MqttCmd::CODE_UNPROCESSABLE;
                logger.log("[MQTTClient] cmd final id=%s code=%u\n", pr.id, (unsigned)pr.code);
                (void)stageReply_(pr);
                return;
            }
            // Sync finish already staged final via lifecycle (_hasActiveRun cleared).
            // Async: final /reply when ProgramExecutor finish() fires lifecycle.
            return;
        }
        case MqttCommandKind::Stop: {
            logger.log("[MQTTClient] stop id=%s\n", cmd.id);
            if (_ports && _ports->control.stopProgram) {
                _ports->control.stopProgram(_ports->control.ctx);
            }
            PendingReply pr{};
            pr.kind = PendingKind::Thin;
            strlcpy(pr.id, cmd.id, sizeof(pr.id));
            pr.code = MqttCmd::CODE_OK;
            logger.log("[MQTTClient] cmd final id=%s code=%u\n", pr.id, (unsigned)pr.code);
            (void)stageReply_(pr);
            return;
        }
        case MqttCommandKind::List: {
            logger.log("[MQTTClient] list id=%s\n", cmd.id);
            PendingReply pr{};
            pr.kind = PendingKind::List;
            strlcpy(pr.id, cmd.id, sizeof(pr.id));
            pr.code = MqttCmd::CODE_OK;
            logger.log("[MQTTClient] cmd final id=%s code=%u\n", pr.id, (unsigned)pr.code);
            (void)stageReply_(pr);
            return;
        }
        case MqttCommandKind::Status: {
            logger.log("[MQTTClient] status id=%s\n", cmd.id);
            PendingReply pr{};
            pr.kind = PendingKind::Status;
            strlcpy(pr.id, cmd.id, sizeof(pr.id));
            pr.code = MqttCmd::CODE_OK;
            logger.log("[MQTTClient] cmd final id=%s code=%u\n", pr.id, (unsigned)pr.code);
            (void)stageReply_(pr);
            return;
        }
        case MqttCommandKind::Set: {
            logger.log("[MQTTClient] set name=%u ref=%u en=%u id=%s\n", (unsigned)cmd.setName, (unsigned)cmd.ref,
                       cmd.enabled ? 1u : 0u, cmd.id);
            bool ok = false;
            MqttCmdErr err = MqttCmdErr::Rejected;
            if (!_ports) {
                err = MqttCmdErr::Rejected;
            } else {
                switch (cmd.setName) {
                    case MqttSetName::Thermostat:
                        if (_ports->control.setThermostat) {
                            _ports->control.setThermostat(_ports->control.ctx, cmd.enabled);
                            ok = true;
                        }
                        break;
                    case MqttSetName::BatterySaver:
                        if (_ports->control.setBatterySaver) {
                            _ports->control.setBatterySaver(_ports->control.ctx, cmd.enabled);
                            ok = true;
                        }
                        break;
                    case MqttSetName::Input:
                        ok = _ports->control.setInputRuntime &&
                             _ports->control.setInputRuntime(_ports->control.ctx, cmd.ref, cmd.enabled);
                        err = ok ? MqttCmdErr::None : MqttCmdErr::NotFound;
                        break;
                    case MqttSetName::Trigger:
                        ok = _ports->control.setInputTrigger &&
                             _ports->control.setInputTrigger(_ports->control.ctx, cmd.ref, cmd.enabled);
                        err = ok ? MqttCmdErr::None : MqttCmdErr::NotFound;
                        break;
                    case MqttSetName::TempTrigger:
                        ok = _ports->control.setTempTrigger &&
                             _ports->control.setTempTrigger(_ports->control.ctx, cmd.ref, cmd.enabled);
                        err = ok ? MqttCmdErr::None : MqttCmdErr::NotFound;
                        break;
                    case MqttSetName::WifiAp:
                        ok = _ports->control.wakeWifiAp &&
                             _ports->control.wakeWifiAp(_ports->control.ctx, cmd.enabled);
                        err = ok ? MqttCmdErr::None : MqttCmdErr::Rejected;
                        break;
                    default:
                        err = MqttCmdErr::Unknown;
                        break;
                }
            }
            const uint16_t code = ok ? MqttCmd::CODE_OK : mqttErrToHttpCode(err);
            PendingReply pr{};
            pr.kind = PendingKind::Thin;
            strlcpy(pr.id, cmd.id, sizeof(pr.id));
            pr.code = code;
            logger.log("[MQTTClient] cmd final id=%s code=%u\n", pr.id, (unsigned)pr.code);
            (void)stageReply_(pr);
            return;
        }
        default: {
            PendingReply pr{};
            pr.kind = PendingKind::Thin;
            strlcpy(pr.id, cmd.id, sizeof(pr.id));
            pr.code = MqttCmd::CODE_BAD_REQUEST;
            logger.log("[MQTTClient] cmd final id=%s code=%u\n", pr.id, (unsigned)pr.code);
            (void)stageReply_(pr);
            return;
        }
    }
}
