#include "mqtt/MQTTClient.h"

#include "app/AppPorts.h"
#include "config/Config.h"
#include "config/internal/ProgramJsonIo.h"
#include "mqtt/MqttCommandParser.h"

#include "common/Constants.h"
#include "common/Logger.h"
#include "core/Core.h"

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

struct ListReplyCtx {
    const char* id;
};

size_t encodeListReply(Print& p, void* ctx) {
    auto* c = reinterpret_cast<const ListReplyCtx*>(ctx);
    CountingForwarder fc(p);
    fc.print("{\"id\":\"");
    fc.print(c->id ? c->id : "");
    fc.print("\",\"cmd\":\"list\",\"ok\":true,\"programs\":");
    (void)config.emitProgramIndexArray(fc);
    fc.print('}');
    return fc.n;
}

struct ThinReplyCtx {
    const char* id;
    const char* cmd;
    bool ok;
    const char* err;
    const char* state;
};

size_t encodeThinReply(Print& p, void* ctx) {
    auto* c = reinterpret_cast<const ThinReplyCtx*>(ctx);
    CountingForwarder fc(p);
    fc.print("{\"id\":\"");
    fc.print(c->id ? c->id : "");
    fc.print('"');
    if (c->cmd && c->cmd[0]) {
        fc.print(",\"cmd\":\"");
        fc.print(c->cmd);
        fc.print('"');
    }
    fc.print(",\"ok\":");
    fc.print(c->ok ? "true" : "false");
    if (!c->ok && c->err && c->err[0]) {
        fc.print(",\"err\":\"");
        fc.print(c->err);
        fc.print('"');
    }
    if (c->state && c->state[0]) {
        fc.print(",\"state\":\"");
        fc.print(c->state);
        fc.print('"');
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
    const MqttRunState st = finishedOk ? MqttRunState::Finished : MqttRunState::Failed;
    const bool ok = finishedOk;
    const MqttCmdErr err = finishedOk ? MqttCmdErr::None : MqttCmdErr::Rejected;
    updateIdemRunState_(_activeRunId, ok, err, st);
    (void)tryPublishThinOrQueue_(_activeRunId, MqttCommandKind::Run, ok, err, st);
    _hasActiveRun = false;
    _activeRunId[0] = '\0';
    _activeRunProgram = 0;
}

void MQTTClient::clearIdem_() {
    for (uint8_t i = 0; i < kIdemDepth; i++) {
        _idem[i] = IdemEntry{};
    }
    _idemNext = 0;
}

void MQTTClient::clearPending_() {
    for (uint8_t i = 0; i < kPendingReplyDepth; i++) {
        _pending[i] = PendingReply{};
    }
    _pendingCount = 0;
}

uint32_t MQTTClient::argsFingerprint_(const MqttCommand& cmd) const {
    uint32_t fp = (uint32_t)cmd.kind << 24;
    fp ^= (uint32_t)cmd.programId << 16;
    fp ^= (uint32_t)cmd.setName << 8;
    fp ^= cmd.ref;
    if (cmd.hasEnabled) fp ^= cmd.enabled ? 1u : 2u;
    return fp;
}

MQTTClient::IdemEntry* MQTTClient::findIdem_(const char* id) {
    if (!id || !id[0]) return nullptr;
    const uint32_t now = millis();
    for (uint8_t i = 0; i < kIdemDepth; i++) {
        IdemEntry& e = _idem[i];
        if (!e.used) continue;
        if ((now - e.atMs) > kIdemTtlMs) {
            e.used = false;
            continue;
        }
        if (strcmp(e.id, id) == 0) return &e;
    }
    return nullptr;
}

void MQTTClient::storeIdem_(const MqttCommand& cmd, bool ok, MqttCmdErr err, MqttRunState st) {
    IdemEntry& e = _idem[_idemNext];
    _idemNext = (uint8_t)((_idemNext + 1u) % kIdemDepth);
    e.used = true;
    strlcpy(e.id, cmd.id, sizeof(e.id));
    e.argsFp = argsFingerprint_(cmd);
    e.kind = cmd.kind;
    e.ok = ok;
    e.err = err;
    e.runState = st;
    e.atMs = millis();
}

void MQTTClient::updateIdemRunState_(const char* id, bool ok, MqttCmdErr err, MqttRunState st) {
    IdemEntry* e = findIdem_(id);
    if (!e) return;
    e->ok = ok;
    e->err = err;
    e->runState = st;
    e->atMs = millis();
}

bool MQTTClient::enqueuePending_(const PendingReply& pr) {
    if (_pendingCount >= kPendingReplyDepth) return false;
    _pending[_pendingCount++] = pr;
    return true;
}

bool MQTTClient::publishThinReply_(const char* id, MqttCommandKind cmd, bool ok, MqttCmdErr err,
                                   MqttRunState st) {
    if (!_topicReply[0]) return false;
    ThinReplyCtx ctx{id, mqttCmdKindStr(cmd), ok, mqttCmdErrStr(err), mqttRunStateStr(st)};
    size_t measured = 0;
    if (!_fsm.publishPrintedMeasured(_topicReply, encodeThinReply, &ctx, 256, false, true, &measured)) {
        return false;
    }
    logger.log("[MQTTClient] pub reply cmd=%s ok=%u bytes=%u\n", mqttCmdKindStr(cmd), ok ? 1u : 0u,
               (unsigned)measured);
    return true;
}

bool MQTTClient::publishListReply_(const char* id) {
    if (!_topicReply[0]) return false;
    ListReplyCtx ctx{id};
    size_t measured = 0;
    // Whole envelope must fit TX; LIST_PROGRAMS_JSON_MAX covers programs+envelope headroom.
    if (!_fsm.publishPrintedMeasured(_topicReply, encodeListReply, &ctx,
                                     JsonBytes::Mqtt::LIST_PROGRAMS_JSON_MAX, false, true, &measured)) {
        if (measured > JsonBytes::Mqtt::LIST_PROGRAMS_JSON_MAX) {
            logger.log("[MQTTClient] list reply too large measured=%u\n", (unsigned)measured);
            return publishThinReply_(id, MqttCommandKind::List, false, MqttCmdErr::Rejected, MqttRunState::None);
        }
        return false;
    }
    logger.log("[MQTTClient] pub reply list bytes=%u\n", (unsigned)measured);
    return true;
}

bool MQTTClient::tryPublishThinOrQueue_(const char* id, MqttCommandKind cmd, bool ok, MqttCmdErr err,
                                        MqttRunState st) {
    if (publishThinReply_(id, cmd, ok, err, st)) return true;
    PendingReply pr{};
    pr.kind = PendingKind::Thin;
    strlcpy(pr.id, id ? id : "", sizeof(pr.id));
    pr.cmd = cmd;
    pr.ok = ok;
    pr.err = err;
    pr.runState = st;
    return enqueuePending_(pr);
}

void MQTTClient::flushPending_() {
    while (_pendingCount > 0) {
        PendingReply& pr = _pending[0];
        bool ok = false;
        if (pr.kind == PendingKind::Thin) {
            ok = publishThinReply_(pr.id, pr.cmd, pr.ok, pr.err, pr.runState);
        } else if (pr.kind == PendingKind::List) {
            ok = publishListReply_(pr.id);
        } else if (pr.kind == PendingKind::StatusAndAck) {
            ok = publishStatus(true) && publishThinReply_(pr.id, MqttCommandKind::Status, true, MqttCmdErr::None,
                                                          MqttRunState::None);
        }
        if (!ok) break;
        for (uint8_t i = 1; i < _pendingCount; i++) _pending[i - 1] = _pending[i];
        _pendingCount--;
        _pending[_pendingCount] = PendingReply{};
    }
}

bool MQTTClient::validateArgs_(const MqttCommand& cmd, MqttCmdErr& errOut) const {
    errOut = MqttCmdErr::None;
    if (cmd.id[0] == '\0' || strlen(cmd.id) > 16) {
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
    if (!_topicCmd[0] || !topic || strcmp(topic, _topicCmd) != 0) return;
    logger.log("[MQTTClient] cmd recv len=%u\n", (unsigned)length);

    char message[JsonBytes::Mqtt::CMD_JSON_MAX];
    if (length == 0 || length >= sizeof(message)) {
        logger.log("[MQTTClient] Command payload too large (%u)\n", (unsigned)length);
        return;
    }
    memcpy(message, payload, length);
    message[length] = '\0';

    if (_cmdBusy || _pendingCount >= kPendingReplyDepth) {
        char idBuf[17]{};
        (void)mqttExtractReqId(message, idBuf, sizeof(idBuf));
        if (idBuf[0]) {
            (void)tryPublishThinOrQueue_(idBuf, MqttCommandKind::None, false, MqttCmdErr::Busy, MqttRunState::None);
        }
        return;
    }

    MqttCommand cmd;
    if (!parseMqttCommandJson(message, cmd)) {
        char idBuf[17]{};
        (void)mqttExtractReqId(message, idBuf, sizeof(idBuf));
        logger.log("[MQTTClient] cmd parse failed\n");
        if (idBuf[0]) {
            (void)tryPublishThinOrQueue_(idBuf, MqttCommandKind::None, false, MqttCmdErr::Parse, MqttRunState::None);
        }
        return;
    }

    MqttCmdErr argErr = MqttCmdErr::None;
    if (!validateArgs_(cmd, argErr)) {
        (void)tryPublishThinOrQueue_(cmd.id, cmd.kind, false, argErr, MqttRunState::None);
        return;
    }

    IdemEntry* hit = findIdem_(cmd.id);
    if (hit) {
        if (hit->kind != cmd.kind || hit->argsFp != argsFingerprint_(cmd)) {
            (void)tryPublishThinOrQueue_(cmd.id, cmd.kind, false, MqttCmdErr::Conflict, MqttRunState::None);
            return;
        }
        // Replay without side effects.
        if (hit->kind == MqttCommandKind::List) {
            if (!publishListReply_(cmd.id)) {
                PendingReply pr{};
                pr.kind = PendingKind::List;
                strlcpy(pr.id, cmd.id, sizeof(pr.id));
                (void)enqueuePending_(pr);
            }
            return;
        }
        if (hit->kind == MqttCommandKind::Status) {
            (void)tryPublishThinOrQueue_(cmd.id, MqttCommandKind::Status, hit->ok, hit->err, MqttRunState::None);
            return;
        }
        (void)tryPublishThinOrQueue_(cmd.id, hit->kind, hit->ok, hit->err, hit->runState);
        return;
    }

    _cmdBusy = true;
    dispatchCommand_(cmd);
    _cmdBusy = false;
}

void MQTTClient::dispatchCommand_(const MqttCommand& cmd) {
    switch (cmd.kind) {
        case MqttCommandKind::Run: {
            logger.log("[MQTTClient] run program=%u id=%s\n", (unsigned)cmd.programId, cmd.id);
            bool started = false;
            if (_ports && _ports->control.startProgram) {
                started = _ports->control.startProgram(_ports->control.ctx, cmd.programId);
            }
            if (!started) {
                storeIdem_(cmd, false, MqttCmdErr::Rejected, MqttRunState::Failed);
                (void)tryPublishThinOrQueue_(cmd.id, MqttCommandKind::Run, false, MqttCmdErr::Rejected,
                                             MqttRunState::Failed);
                return;
            }
            strlcpy(_activeRunId, cmd.id, sizeof(_activeRunId));
            _activeRunProgram = cmd.programId;
            _hasActiveRun = true;
            storeIdem_(cmd, true, MqttCmdErr::None, MqttRunState::Accepted);
            (void)tryPublishThinOrQueue_(cmd.id, MqttCommandKind::Run, true, MqttCmdErr::None, MqttRunState::Accepted);
            return;
        }
        case MqttCommandKind::Stop: {
            logger.log("[MQTTClient] stop id=%s\n", cmd.id);
            if (_ports && _ports->control.stopProgram) {
                _ports->control.stopProgram(_ports->control.ctx);
            }
            // stop() notifies lifecycle failed for active run if any.
            storeIdem_(cmd, true, MqttCmdErr::None, MqttRunState::None);
            (void)tryPublishThinOrQueue_(cmd.id, MqttCommandKind::Stop, true, MqttCmdErr::None, MqttRunState::None);
            return;
        }
        case MqttCommandKind::List: {
            logger.log("[MQTTClient] list id=%s\n", cmd.id);
            if (publishListReply_(cmd.id)) {
                storeIdem_(cmd, true, MqttCmdErr::None, MqttRunState::None);
            } else {
                PendingReply pr{};
                pr.kind = PendingKind::List;
                strlcpy(pr.id, cmd.id, sizeof(pr.id));
                if (enqueuePending_(pr)) {
                    storeIdem_(cmd, true, MqttCmdErr::None, MqttRunState::None);
                } else {
                    (void)tryPublishThinOrQueue_(cmd.id, MqttCommandKind::List, false, MqttCmdErr::Busy,
                                                 MqttRunState::None);
                }
            }
            return;
        }
        case MqttCommandKind::Status: {
            logger.log("[MQTTClient] status id=%s\n", cmd.id);
            if (publishStatus(true)) {
                storeIdem_(cmd, true, MqttCmdErr::None, MqttRunState::None);
                (void)tryPublishThinOrQueue_(cmd.id, MqttCommandKind::Status, true, MqttCmdErr::None,
                                             MqttRunState::None);
                _lastStatusPublish = millis();
                core.cooperate();
            } else {
                PendingReply pr{};
                pr.kind = PendingKind::StatusAndAck;
                strlcpy(pr.id, cmd.id, sizeof(pr.id));
                pr.cmd = MqttCommandKind::Status;
                pr.ok = true;
                if (enqueuePending_(pr)) {
                    storeIdem_(cmd, true, MqttCmdErr::None, MqttRunState::None);
                } else {
                    (void)tryPublishThinOrQueue_(cmd.id, MqttCommandKind::Status, false, MqttCmdErr::Busy,
                                                 MqttRunState::None);
                }
            }
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
                    default:
                        err = MqttCmdErr::Unknown;
                        break;
                }
            }
            if (ok) err = MqttCmdErr::None;
            storeIdem_(cmd, ok, err, MqttRunState::None);
            (void)tryPublishThinOrQueue_(cmd.id, MqttCommandKind::Set, ok, err, MqttRunState::None);
            return;
        }
        default:
            (void)tryPublishThinOrQueue_(cmd.id, cmd.kind, false, MqttCmdErr::Unknown, MqttRunState::None);
            return;
    }
}
