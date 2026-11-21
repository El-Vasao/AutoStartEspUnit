#include "mqtt/MqttClient.h"

#include "app/AppPorts.h"
#include "config/Config.h"
#include "mqtt/HwMapJson.h"
#include "mqtt/MqttCommandParser.h"
#include "mqtt/MqttStatusBuilder.h"

#include "common/Constants.h"
#include "common/JsonEscape.h"
#include "common/Logger.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "core/TimeSyncManager.h"
#include "program/ProgramExecutor.h"
#include "mqtt/internal/CountingForwarder.h"

#include <cstring>

namespace {

struct ThinReplyCtx {
    const char* id;
    uint16_t code;
    uint32_t statusSeq;
    bool hasSeq;
};

size_t encodeThinReply(Print& p, void* ctx) {
    auto* c = reinterpret_cast<const ThinReplyCtx*>(ctx);
    CountingForwarder fc(p);
    fc.print("{\"id\":\"");
    json_escape::writeEscaped(fc, c->id ? c->id : "");
    fc.print("\",\"code\":");
    fc.print(c->code);
    if (c->hasSeq) {
        fc.print(",\"seq\":");
        fc.print(static_cast<unsigned long>(c->statusSeq));
    }
    fc.print('}');
    return fc.n;
}

} // namespace

void MqttClient::onProgramLifecycleThunk(void* ctx, uint8_t programId, bool finishedOk) {
    if (!ctx) return;
    static_cast<MqttClient*>(ctx)->onProgramLifecycle(programId, finishedOk);
}

void MqttClient::replyRunFinal_(const char* id, bool finishedOk) {
    if (!id || !id[0]) return;
    const uint16_t code = finishedOk ? MqttCmd::CODE_OK : MqttCmd::CODE_INTERNAL;
    PendingReply pr{};
    pr.kind = PendingKind::Thin;
    strlcpy(pr.id, id, sizeof(pr.id));
    pr.code = code;
    finalizeIdRing_(pr.id, code);
    logger.log(LogModule::MqttClient, "command final id=%s code=%u\n", pr.id, (unsigned)code);
    (void)stageReply_(pr);
}

void MqttClient::onProgramLifecycle(uint8_t programId, bool finishedOk) {
    const bool preemptMatch =
        _hasPreemptedRun && _preemptedRunProgram == programId && _preemptedRunId[0] != '\0';
    const bool activeMatch = _hasActiveRun && _activeRunProgram == programId && _activeRunId[0] != '\0';
    // Пока новый прогон ещё идёт, callback — это finish() прерванного, даже при том же program id.
    if (preemptMatch && !(activeMatch && !core.getProgramExecutor().isRunning())) {
        replyRunFinal_(_preemptedRunId, finishedOk);
        _hasPreemptedRun = false;
        _preemptedRunId[0] = '\0';
        _preemptedRunProgram = 0;
        return;
    }
    if (activeMatch && _hasPreemptedRun && _preemptedRunId[0] != '\0') {
        // У исполнителя один слот ожидания: finish() нового прогона затёр callback прерванного id.
        if (strcmp(_preemptedRunId, _activeRunId) != 0) replyRunFinal_(_preemptedRunId, false);
        _hasPreemptedRun = false;
        _preemptedRunId[0] = '\0';
        _preemptedRunProgram = 0;
    }
    if (!activeMatch) return;
    replyRunFinal_(_activeRunId, finishedOk);
    _hasActiveRun = false;
    _activeRunId[0] = '\0';
    _activeRunProgram = 0;
}

void MqttClient::clearPending_() {
    for (uint8_t i = 0; i < MqttCmd::PENDING_REPLY_DEPTH; i++) {
        _pending[i] = PendingReply{};
    }
    _pendingCount = 0;
}

void MqttClient::clearInbound_() {
    _inboundHead = 0;
    _inboundCount = 0;
    for (uint8_t i = 0; i < MqttCmd::INBOUND_DEPTH; i++) {
        _inbound[i] = MqttCommand{};
    }
}

void MqttClient::clearIdRing_() {
    for (uint8_t i = 0; i < MqttCmd::ID_RING_DEPTH; i++) {
        _idRing[i] = IdRingEntry{};
    }
    _idRingNext = 0;
}

MqttClient::IdRingEntry* MqttClient::findIdRing_(const char* id) {
    if (!id || !id[0]) return nullptr;
    for (uint8_t i = 0; i < MqttCmd::ID_RING_DEPTH; i++) {
        if (!_idRing[i].used) continue;
        if (strcmp(_idRing[i].id, id) == 0) return &_idRing[i];
    }
    return nullptr;
}

bool MqttClient::reserveIdRing_(const char* id, uint16_t* existingCodeOut, bool* existingInFlightOut,
                                bool* noSlotOut) {
    if (existingCodeOut) *existingCodeOut = 0;
    if (existingInFlightOut) *existingInFlightOut = false;
    if (noSlotOut) *noSlotOut = false;
    IdRingEntry* hit = findIdRing_(id);
    if (hit) {
        if (existingCodeOut) *existingCodeOut = hit->code;
        if (existingInFlightOut) *existingInFlightOut = hit->inFlight;
        return false;
    }
    // Не затираем слот, чей id ещё выполняется: иначе повтор этой команды уйдёт в новый прогон.
    int16_t chosen = -1;
    for (uint8_t n = 0; n < MqttCmd::ID_RING_DEPTH; n++) {
        const uint8_t i = (uint8_t)((_idRingNext + n) % MqttCmd::ID_RING_DEPTH);
        if (_idRing[i].inFlight) continue;
        chosen = (int16_t)i;
        break;
    }
    if (chosen < 0) {
        if (noSlotOut) *noSlotOut = true;
        return false;
    }
    IdRingEntry& slot = _idRing[(uint8_t)chosen];
    _idRingNext = (uint8_t)((chosen + 1) % MqttCmd::ID_RING_DEPTH);
    slot = IdRingEntry{};
    strlcpy(slot.id, id ? id : "", sizeof(slot.id));
    slot.inFlight = true;
    slot.used = true;
    slot.code = MqttCmd::CODE_ACCEPTED;
    return true;
}

void MqttClient::releaseIdRing_(const char* id) {
    // Ack или очередь не приняли команду — иначе слот навсегда останется inFlight.
    IdRingEntry* hit = findIdRing_(id);
    if (!hit || !hit->inFlight) return;
    *hit = IdRingEntry{};
}

void MqttClient::finalizeIdRing_(const char* id, uint16_t code) {
    IdRingEntry* hit = findIdRing_(id);
    if (!hit) return;
    hit->inFlight = false;
    hit->code = code;
}

bool MqttClient::enqueueInbound_(const MqttCommand& cmd) {
    if (_inboundCount >= MqttCmd::INBOUND_DEPTH) return false;
    const uint8_t tail = (uint8_t)((_inboundHead + _inboundCount) % MqttCmd::INBOUND_DEPTH);
    _inbound[tail] = cmd;
    _inboundCount++;
    logger.log(LogModule::MqttClient, "command queued n=%u id=%s\n", (unsigned)_inboundCount, cmd.id);
    return true;
}

void MqttClient::drainInbound_() {
    while (_inboundCount > 0 && !_cmdBusy && !ctrlPlaneBlocked_()) {
        MqttCommand cmd = _inbound[_inboundHead];
        _inbound[_inboundHead] = MqttCommand{};
        _inboundHead = (uint8_t)((_inboundHead + 1u) % MqttCmd::INBOUND_DEPTH);
        _inboundCount--;
        logger.log(LogModule::MqttClient, "command parse kind=%s id=%s\n", mqttCmdKindStr(cmd.kind), cmd.id);
        _cmdBusy = true;
        dispatchCommand_(cmd);
        _cmdBusy = false;
        break;
    }
}

bool MqttClient::enqueuePending_(const PendingReply& pr) {
    if (_pendingCount >= MqttCmd::PENDING_REPLY_DEPTH) return false;
    _pending[_pendingCount++] = pr;
    return true;
}

bool MqttClient::publishThinReply_(const char* id, uint16_t code, uint32_t statusSeq) {
    if (!_topicReply[0]) return false;
    // statusSeq==0 → поле seq опускается (тонкий ack/итоговый). Итоговый ответ статуса передаёт seq >= 1.
    ThinReplyCtx ctx{id, code, statusSeq, statusSeq != 0};
    size_t measured = 0;
    if (!_fsm.publishPrintedMeasured(_topicReply, encodeThinReply, &ctx,
                                     JsonBytes::Mqtt::THIN_REPLY_JSON_MAX, false, true, &measured,
                                     MqttFsmClient::TeleKind::None, 1 /*QoS1*/)) {
        return false;
    }
    logger.log(LogModule::MqttClient, "pub reply id=%s code=%u bytes=%u\n", id ? id : "", (unsigned)code,
               (unsigned)measured);
    return true;
}

bool MqttClient::ctrlPlaneBlocked_() const {
    if (_fsm.hasCtrlOutbound()) return true;
    if (_ctrlPlaneBusy && _ctrlPlaneBusy(_ctrlPlaneBusyCtx)) return true;
    return false;
}

bool MqttClient::stageReply_(const PendingReply& pr) {
    if (ctrlPlaneBlocked_()) {
        if (enqueuePending_(pr)) {
            logger.log(LogModule::MqttClient, "reply waiting Ctrl id=%s code=%u kind=%u\n", pr.id, (unsigned)pr.code,
                       (unsigned)pr.kind);
            return true;
        }
        logger.log(LogModule::MqttClient, "reply queue full id=%s code=%u\n", pr.id, (unsigned)pr.code);
        return false;
    }

    bool ok = false;
    if (pr.kind == PendingKind::Thin || pr.kind == PendingKind::Status) {
        ok = publishThinReply_(pr.id, pr.code, pr.statusSeq);
    } else if (pr.kind == PendingKind::List) {
        // Тело с retain=false, чтобы слот хеша у брокера остался; затем тонкий OK.
        if (publishProgramsBody_()) {
            ok = publishThinReply_(pr.id, MqttCmd::CODE_OK);
        } else {
            ok = publishThinReply_(pr.id, MqttCmd::CODE_UNPROCESSABLE);
        }
    } else if (pr.kind == PendingKind::HwMap) {
        const bool a = publishHwMapBody_();
        const bool b = publishTrigMapBody_();
        if (a && b) {
            ok = publishThinReply_(pr.id, MqttCmd::CODE_OK);
        } else {
            ok = publishThinReply_(pr.id, MqttCmd::CODE_UNPROCESSABLE);
        }
    }

    if (ok) return true;

    if (enqueuePending_(pr)) {
        logger.log(LogModule::MqttClient, "reply queued kind=%u id=%s code=%u\n", (unsigned)pr.kind, pr.id,
                   (unsigned)pr.code);
        return true;
    }
    logger.log(LogModule::MqttClient, "reply queue full id=%s code=%u\n", pr.id, (unsigned)pr.code);
    return false;
}

void MqttClient::flushPending_() {
    while (_pendingCount > 0) {
        if (ctrlPlaneBlocked_()) break;
        PendingReply& pr = _pending[0];
        bool ok = false;
        if (pr.kind == PendingKind::Thin || pr.kind == PendingKind::Status) {
            ok = publishThinReply_(pr.id, pr.code, pr.statusSeq);
        } else if (pr.kind == PendingKind::List) {
            if (publishProgramsBody_()) {
                ok = publishThinReply_(pr.id, MqttCmd::CODE_OK);
            } else {
                ok = publishThinReply_(pr.id, MqttCmd::CODE_UNPROCESSABLE);
            }
        } else if (pr.kind == PendingKind::HwMap) {
            const bool a = publishHwMapBody_();
            const bool b = publishTrigMapBody_();
            ok = publishThinReply_(pr.id, (a && b) ? MqttCmd::CODE_OK : MqttCmd::CODE_UNPROCESSABLE);
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

bool MqttClient::validateArgs_(const MqttCommand& cmd, MqttCmdErr& errOut) const {
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
        case MqttCommandKind::Seq:
        case MqttCommandKind::HwMap:
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
        case MqttCommandKind::SetTime:
            if (!cmd.hasEpoch || cmd.epoch < (uint32_t)TimeSync::MIN_SANE_EPOCH_UTC) {
                errOut = MqttCmdErr::Args;
                return false;
            }
            return true;
        default:
            errOut = MqttCmdErr::Unknown;
            return false;
    }
}

void MqttClient::onPublishThunk(void* ctx, const char* topic, const uint8_t* payload, uint16_t len, bool retained) {
    if (!ctx) return;
    static_cast<MqttClient*>(ctx)->handlePublish(topic, payload, len, retained);
}

void MqttClient::handlePublish(const char* topic, const uint8_t* payload, uint16_t length, bool retained) {
    if (!_topicCmd[0]) {
        logger.log(LogModule::MqttClient, "command dropped: empty topicCmd\n");
        return;
    }
    if (!topic) {
        logger.log(LogModule::MqttClient, "command dropped: topic empty\n");
        return;
    }
    if (strcmp(topic, _topicCmd) != 0) {
        logger.log(LogModule::MqttClient, "foreign topic received=%s expected=%s\n", topic, _topicCmd);
        return;
    }
    // Команду cmd с retain выполнять нельзя (устаревший run заводил бы машину на каждом сеансе).
    if (retained) {
        logger.log(LogModule::MqttClient, "command skipped: retained\n");
        return;
    }
    tryAckIngress_(payload, length);
}

void MqttClient::tryAckIngress_(const uint8_t* payload, uint16_t length) {
    logger.log(LogModule::MqttClient, "command accepted, len=%u\n", (unsigned)length);

    char message[JsonBytes::Mqtt::CMD_JSON_MAX];
    if (length == 0 || length >= sizeof(message)) {
        logger.log(LogModule::MqttClient, "command payload too large (%u)\n", (unsigned)length);
        return;
    }
    if (!payload) return;
    memcpy(message, payload, length);
    message[length] = '\0';

    auto stageAck = [this](const char* id, uint16_t code) -> bool {
        PendingReply pr{};
        pr.kind = PendingKind::Thin;
        strlcpy(pr.id, id, sizeof(pr.id));
        pr.code = code;
        logger.log(LogModule::MqttClient, "command ack id=%s code=%u\n", id, (unsigned)code);
        if (!stageReply_(pr)) {
            logger.log(LogModule::MqttClient, "command ack not queued id=%s code=%u\n", id, (unsigned)code);
            return false;
        }
        return true;
    };

    MqttCommand cmd;
    if (!parseMqttCommandJson(message, cmd)) {
        char idBuf[MqttCmd::ID_MAX_LEN + 1]{};
        (void)mqttExtractReqId(message, idBuf, sizeof(idBuf));
        logger.log(LogModule::MqttClient, "command not recognized\n");
        if (idBuf[0]) (void)stageAck(idBuf, MqttCmd::CODE_BAD_REQUEST);
        return;
    }

    MqttCmdErr argErr = MqttCmdErr::None;
    if (!validateArgs_(cmd, argErr)) {
        logger.log(LogModule::MqttClient, "command: invalid id=%s\n", cmd.id);
        (void)stageAck(cmd.id, mqttErrToHttpCode(argErr));
        return;
    }

    const bool idempotentKind = (cmd.kind == MqttCommandKind::Run || cmd.kind == MqttCommandKind::Stop ||
                                 cmd.kind == MqttCommandKind::Set || cmd.kind == MqttCommandKind::SetTime);
    if (idempotentKind) {
        uint16_t existingCode = 0;
        bool inFlight = false;
        bool noSlot = false;
        if (!reserveIdRing_(cmd.id, &existingCode, &inFlight, &noSlot)) {
            if (noSlot) {
                (void)stageAck(cmd.id, MqttCmd::CODE_UNAVAILABLE);
            } else if (inFlight) {
                (void)stageAck(cmd.id, MqttCmd::CODE_ACCEPTED);
            } else {
                (void)stageAck(cmd.id, existingCode ? existingCode : MqttCmd::CODE_OK);
            }
            return;
        }
    }

    if (_inboundCount >= MqttCmd::INBOUND_DEPTH) {
        logger.log(LogModule::MqttClient, "incoming command queue full id=%s\n", cmd.id);
        if (idempotentKind) releaseIdRing_(cmd.id);
        (void)stageAck(cmd.id, MqttCmd::CODE_UNAVAILABLE);
        return;
    }

    if (!stageAck(cmd.id, MqttCmd::CODE_ACCEPTED)) {
        logger.log(LogModule::MqttClient, "command not queued (ack busy) id=%s\n", cmd.id);
        if (idempotentKind) releaseIdRing_(cmd.id);
        return;
    }
    logger.log(LogModule::MqttClient, "command ack kind=%s id=%s\n", mqttCmdKindStr(cmd.kind), cmd.id);
    if (!enqueueInbound_(cmd) && idempotentKind) releaseIdRing_(cmd.id);
}

void MqttClient::dispatchCommand_(const MqttCommand& cmd) {
    switch (cmd.kind) {
        case MqttCommandKind::Run: {
            logger.log(LogModule::MqttClient, "run program=%u id=%s\n", (unsigned)cmd.programId, cmd.id);
            // start() зовёт finish() без allOff при удачной передаче; жизненный цикл отложен.
            // Новый id до start() закрылся бы этим callback'ом как провал прерванного прогона.
            const bool preempt = _hasActiveRun && _activeRunId[0] != '\0';
            if (preempt) {
                if (_hasPreemptedRun && _preemptedRunId[0] != '\0') {
                    // Один слот ожидания: новый finish() затрёт ещё не доставленный callback.
                    replyRunFinal_(_preemptedRunId, false);
                }
                strlcpy(_preemptedRunId, _activeRunId, sizeof(_preemptedRunId));
                _preemptedRunProgram = _activeRunProgram;
                _hasPreemptedRun = true;
                _hasActiveRun = false;
                _activeRunId[0] = '\0';
                _activeRunProgram = 0;
            }
            bool started = false;
            if (_ports && _ports->control.startProgram) {
                started = _ports->control.startProgram(_ports->control.ctx, cmd.programId);
            }
            if (started && !core.getProgramExecutor().isRunning() && _hasPreemptedRun && _preemptedRunId[0] != '\0') {
                // finish() нового прогона внутри start() затёр callback прерванного id.
                replyRunFinal_(_preemptedRunId, false);
                _hasPreemptedRun = false;
                _preemptedRunId[0] = '\0';
                _preemptedRunProgram = 0;
            }
            if (!started) {
                PendingReply pr{};
                pr.kind = PendingKind::Thin;
                strlcpy(pr.id, cmd.id, sizeof(pr.id));
                pr.code = MqttCmd::CODE_UNPROCESSABLE;
                finalizeIdRing_(pr.id, pr.code);
                logger.log(LogModule::MqttClient, "command final id=%s code=%u\n", pr.id, (unsigned)pr.code);
                (void)stageReply_(pr);
                return;
            }
            strlcpy(_activeRunId, cmd.id, sizeof(_activeRunId));
            _activeRunProgram = cmd.programId;
            _hasActiveRun = true;
            return;
        }
        case MqttCommandKind::Stop: {
            logger.log(LogModule::MqttClient, "stop id=%s\n", cmd.id);
            if (_hasActiveRun && _activeRunId[0] != '\0' && core.getProgramExecutor().isRunning()) {
                if (_hasPreemptedRun && _preemptedRunId[0] != '\0') {
                    replyRunFinal_(_preemptedRunId, false);
                }
                // stop() тоже откладывает finish(); id прогона нужен callback'у, а не сбрасывается заранее.
                strlcpy(_preemptedRunId, _activeRunId, sizeof(_preemptedRunId));
                _preemptedRunProgram = _activeRunProgram;
                _hasPreemptedRun = true;
            }
            _hasActiveRun = false;
            _activeRunId[0] = '\0';
            _activeRunProgram = 0;
            if (_ports && _ports->control.stopProgram) {
                _ports->control.stopProgram(_ports->control.ctx);
            }
            PendingReply pr{};
            pr.kind = PendingKind::Thin;
            strlcpy(pr.id, cmd.id, sizeof(pr.id));
            pr.code = MqttCmd::CODE_OK;
            finalizeIdRing_(pr.id, pr.code);
            logger.log(LogModule::MqttClient, "command final id=%s code=%u\n", pr.id, (unsigned)pr.code);
            (void)stageReply_(pr);
            return;
        }
        case MqttCommandKind::List: {
            logger.log(LogModule::MqttClient, "list id=%s\n", cmd.id);
            PendingReply pr{};
            pr.kind = PendingKind::List;
            strlcpy(pr.id, cmd.id, sizeof(pr.id));
            pr.code = MqttCmd::CODE_OK;
            (void)stageReply_(pr);
            return;
        }
        case MqttCommandKind::Status: {
            logger.log(LogModule::MqttClient, "status id=%s\n", cmd.id);
            // Всегда заново кодировать свежий полный state с retain.
            if (!publishStatus(true)) {
                PendingReply pr{};
                pr.kind = PendingKind::Thin;
                strlcpy(pr.id, cmd.id, sizeof(pr.id));
                pr.code = MqttCmd::CODE_UNAVAILABLE;
                (void)stageReply_(pr);
                return;
            }
            _lastStatusPublish = millis();
            PendingReply pr{};
            pr.kind = PendingKind::Status;
            strlcpy(pr.id, cmd.id, sizeof(pr.id));
            pr.code = MqttCmd::CODE_OK;
            pr.statusSeq = _haveQueuedStateSeq ? _queuedStateSeq : (_haveSeq ? _statusSeq : 0);
            (void)stageReply_(pr);
            return;
        }
        case MqttCommandKind::Seq: {
            logger.log(LogModule::MqttClient, "seq id=%s\n", cmd.id);
            // Последний кадр, вышедший из очереди модема — state не публикуем.
            PendingReply pr{};
            pr.kind = PendingKind::Status;
            strlcpy(pr.id, cmd.id, sizeof(pr.id));
            pr.code = MqttCmd::CODE_OK;
            pr.statusSeq = _haveDeliveredSeq ? _deliveredSeq : 0;
            (void)stageReply_(pr);
            return;
        }
        case MqttCommandKind::HwMap: {
            logger.log(LogModule::MqttClient, "hwmap id=%s\n", cmd.id);
            PendingReply pr{};
            pr.kind = PendingKind::HwMap;
            strlcpy(pr.id, cmd.id, sizeof(pr.id));
            pr.code = MqttCmd::CODE_OK;
            (void)stageReply_(pr);
            return;
        }
        case MqttCommandKind::Set: {
            logger.log(LogModule::MqttClient, "set name=%u ref=%u on=%u id=%s\n", (unsigned)cmd.setName,
                       (unsigned)cmd.ref, cmd.enabled ? 1u : 0u, cmd.id);
            bool ok = false;
            MqttCmdErr err = MqttCmdErr::Rejected;
            if (_ports) {
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
            finalizeIdRing_(pr.id, code);
            logger.log(LogModule::MqttClient, "command final id=%s code=%u\n", pr.id, (unsigned)pr.code);
            (void)stageReply_(pr);
            return;
        }
        case MqttCommandKind::SetTime: {
            logger.log(LogModule::MqttClient, "set_time epoch=%lu id=%s\n", (unsigned long)cmd.epoch, cmd.id);
            core.applyWallClockEpoch(cmd.epoch);
            PendingReply pr{};
            pr.kind = PendingKind::Thin;
            strlcpy(pr.id, cmd.id, sizeof(pr.id));
            pr.code = MqttCmd::CODE_OK;
            finalizeIdRing_(pr.id, pr.code);
            logger.log(LogModule::MqttClient, "command final id=%s code=%u\n", pr.id, (unsigned)pr.code);
            (void)stageReply_(pr);
            return;
        }
        default: {
            PendingReply pr{};
            pr.kind = PendingKind::Thin;
            strlcpy(pr.id, cmd.id, sizeof(pr.id));
            pr.code = MqttCmd::CODE_BAD_REQUEST;
            (void)stageReply_(pr);
            return;
        }
    }
}
