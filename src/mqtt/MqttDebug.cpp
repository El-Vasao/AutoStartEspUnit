#if defined(AUTOSTART_MQTT_DEBUG)

#include "mqtt/MqttDebug.h"

#include "common/Constants.h"
#include "common/ErrorCodes.h"
#include "common/Logger.h"
#include "config/Config.h"
#include "config/ConfigTypes.h"
#include "config/internal/ProgramJsonIo.h"
#include "core/Core.h"
#include "core/FlashCommitCoordinator.h"
#include "fs/FsManager.h"
#include "gsm/GsmController.h"
#include "io/DigitalInputs.h"
#include "io/RelayController.h"
#include "io/SensorsController.h"
#include "mqtt/MqttClient.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

MqttDebugSession mqttDebugSession;

namespace {

/// После flash-записи SIM800 RX часто копится (SoftAP+TX) → rx_incomplete на следующем cmd.
/// Короткая прокачка UART/TCP до финального reply.
void settleModemRxAfterFlash_() {
    auto& gsm = core.getGSM();
    const uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < 200u) {
        gsm.pollModemRx();
        core.feedWatchdog();
        core.cooperate();
        yield();
    }
}

class BufPrint : public Print {
public:
    explicit BufPrint(uint8_t* buf, size_t cap) : buf_(buf), cap_(cap) {}
    size_t write(uint8_t c) override {
        if (len_ >= cap_) {
            overflow_ = true;
            return 0;
        }
        buf_[len_++] = c;
        return 1;
    }
    size_t write(const uint8_t* buffer, size_t size) override {
        size_t n = 0;
        while (n < size) {
            if (!write(buffer[n])) break;
            n++;
        }
        return n;
    }
    size_t length() const { return len_; }
    bool overflow() const { return overflow_; }

private:
    uint8_t* buf_;
    size_t cap_;
    size_t len_{0};
    bool overflow_{false};
};

constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64Index(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool parseStartMethod(const char* v, StartMethod* out) {
    if (!v || !out) return false;
    if (strcmp(v, "timed") == 0) {
        *out = StartMethod::Timed;
        return true;
    }
    if (strcmp(v, "rpm") == 0) {
        *out = StartMethod::Rpm;
        return true;
    }
    if (strcmp(v, "input") == 0) {
        *out = StartMethod::Input;
        return true;
    }
    return false;
}

constexpr size_t kRawPerChunk = 270;

} // namespace

uint32_t mqttDebugCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            const uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

bool mqttDebugHexToU32(const char* hex8, uint32_t* out) {
    if (!hex8 || !out || strlen(hex8) != 8) return false;
    uint32_t v = 0;
    for (int i = 0; i < 8; i++) {
        const char c = hex8[i];
        uint8_t nibble = 0;
        if (c >= '0' && c <= '9') nibble = static_cast<uint8_t>(c - '0');
        else if (c >= 'a' && c <= 'f') nibble = static_cast<uint8_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') nibble = static_cast<uint8_t>(c - 'A' + 10);
        else return false;
        v = (v << 4) | nibble;
    }
    *out = v;
    return true;
}

void mqttDebugU32ToHex(uint32_t v, char out[9]) {
    static const char* hex = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        out[i] = hex[v & 0xF];
        v >>= 4;
    }
    out[8] = '\0';
}

size_t mqttDebugBase64Decode(const char* b64, uint8_t* out, size_t outCap) {
    if (!b64 || !out) return 0;
    size_t w = 0;
    int val = 0;
    int valb = -8;
    for (const char* p = b64; *p; ++p) {
        if (*p == '=') break;
        if (isspace((unsigned char)*p)) continue;
        const int d = b64Index(*p);
        if (d < 0) return 0;
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            if (w >= outCap) return 0;
            out[w++] = static_cast<uint8_t>((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return w;
}

size_t mqttDebugBase64Encode(const uint8_t* in, size_t inLen, char* out, size_t outCap) {
    if (!out || outCap == 0) return 0;
    if (inLen == 0) {
        out[0] = '\0';
        return 0;
    }
    if (!in) return 0;
    size_t o = 0;
    size_t i = 0;
    while (i < inLen) {
        const size_t rem = inLen - i;
        const uint32_t a = in[i++];
        const uint32_t b = rem > 1 ? in[i++] : 0;
        const uint32_t c = rem > 2 ? in[i++] : 0;
        const uint32_t triple = (a << 16) | (b << 8) | c;
        if (o + 5 > outCap) return 0;
        out[o++] = kB64[(triple >> 18) & 0x3F];
        out[o++] = kB64[(triple >> 12) & 0x3F];
        out[o++] = rem > 1 ? kB64[(triple >> 6) & 0x3F] : '=';
        out[o++] = rem > 2 ? kB64[triple & 0x3F] : '=';
    }
    out[o] = '\0';
    return o;
}

bool MqttDebugSession::holdPutB64(const char* b64) {
    if (!b64 || !*b64) return false;
    if (strlen(b64) > MqttDebugLimits::CHUNK_B64_MAX) return false;
    strlcpy(holdB64_, b64, sizeof(holdB64_));
    return true;
}

void MqttDebugSession::clearUploadFields_() {
    expectBytes_ = 0;
    gotBytes_ = 0;
    expectCrc_ = 0;
    expectN_ = 0;
    nextI_ = 0;
    programId_ = 0;
    holdB64_[0] = '\0';
}

void MqttDebugSession::abort() {
    // Flash already owns deferred body — do not dropPostedBody here.
    state_ = State::Idle;
    target_ = Target::None;
    clearUploadFields_();
    pendingReplyId_[0] = '\0';
    sendI_ = 0;
    sendN_ = 0;
    sendLen_ = 0;
    sendOff_ = 0;
}

uint16_t MqttDebugSession::begin_(const MqttCommand& cmd, Target t) {
    if (state_ != State::Idle) return MqttCmd::CODE_UNAVAILABLE;
    if (!cmd.hasDebugBytes || cmd.debugBytes == 0 || cmd.debugBytes > MqttDebugLimits::PUT_MAX) {
        return MqttCmd::CODE_BAD_REQUEST;
    }
    uint32_t crc = 0;
    if (!mqttDebugHexToU32(cmd.debugCrcHex, &crc)) return MqttCmd::CODE_BAD_REQUEST;
    if (t == Target::Prg && cmd.programId == 0) return MqttCmd::CODE_BAD_REQUEST;

    state_ = State::Receiving;
    target_ = t;
    expectBytes_ = cmd.debugBytes;
    gotBytes_ = 0;
    expectCrc_ = crc;
    expectN_ = 0;
    nextI_ = 0;
    programId_ = cmd.programId;
    holdB64_[0] = '\0';
    return MqttCmd::CODE_OK;
}

uint16_t MqttDebugSession::put_(const MqttCommand& cmd) {
    if (state_ != State::Receiving) return MqttCmd::CODE_CONFLICT;
    if (!cmd.hasDebugChunk || cmd.debugChunkN == 0 || cmd.debugChunkI >= cmd.debugChunkN) {
        return MqttCmd::CODE_BAD_REQUEST;
    }
    if (expectN_ == 0) {
        expectN_ = cmd.debugChunkN;
    } else if (cmd.debugChunkN != expectN_) {
        return MqttCmd::CODE_BAD_REQUEST;
    }
    if (cmd.debugChunkI != nextI_) return MqttCmd::CODE_BAD_REQUEST;
    if (!holdB64_[0]) return MqttCmd::CODE_BAD_REQUEST;

    const size_t room = expectBytes_ - gotBytes_;
    uint8_t decoded[MqttDebugLimits::CHUNK_B64_MAX];
    const size_t n = mqttDebugBase64Decode(holdB64_, decoded, sizeof(decoded));
    holdB64_[0] = '\0';
    if (n == 0 || n > room) return MqttCmd::CODE_BAD_REQUEST;
    memcpy(buf_ + gotBytes_, decoded, n);
    gotBytes_ += static_cast<uint32_t>(n);
    nextI_++;
    return MqttCmd::CODE_OK;
}

uint16_t MqttDebugSession::commit_(SensorsController& sensors) {
    (void)sensors; // apply runs in FlashCommitCoordinator::tick via core.getSensors()
    if (state_ != State::Receiving) return MqttCmd::CODE_CONFLICT;
    if (expectN_ == 0 || nextI_ != expectN_) return MqttCmd::CODE_BAD_REQUEST;
    if (gotBytes_ != expectBytes_) return MqttCmd::CODE_BAD_REQUEST;
    const uint32_t crc = mqttDebugCrc32(buf_, gotBytes_);
    if (crc != expectCrc_) {
        abort();
        return MqttCmd::CODE_UNPROCESSABLE;
    }

    const Target t = target_;
    if (gotBytes_ == 0 || gotBytes_ > Limits::CONFIG_JSON_SIZE) {
        abort();
        return MqttCmd::CODE_BAD_REQUEST;
    }

    // SoftAP parity: arm RAM body + defer FS apply outside MqttClient::drainInbound_.
    if (flashCommit.isPending()) return MqttCmd::CODE_UNAVAILABLE;
    if (!flashCommit.armPostedBody(t == Target::Cfg, gotBytes_)) return MqttCmd::CODE_UNAVAILABLE;
    if (!flashCommit.appendPostedBody(0, buf_, gotBytes_)) {
        flashCommit.dropPostedBody();
        abort();
        return MqttCmd::CODE_INTERNAL;
    }
    if (t == Target::Cfg) {
        flashCommit.deferPostedConfigApply();
    } else {
        flashCommit.deferPostedProgramApply();
    }

    clearUploadFields_();
    target_ = Target::None;
    state_ = State::CommitPending;
    logger.log(LogModule::MqttClient, "debug commit deferred (%s)\n",
               t == Target::Cfg ? "cfg" : "prg");
    return 0; // final /reply after flashCommit.tick
}

uint16_t MqttDebugSession::doSet_(const MqttCommand& cmd) {
    if (state_ != State::Idle) return MqttCmd::CODE_UNAVAILABLE;
    if (cmd.debugPath[0] == '\0' || cmd.debugValue[0] == '\0') return MqttCmd::CODE_BAD_REQUEST;

    BaseConfig& base = config.debugMutableBase();
    bool changed = false;

    if (strcmp(cmd.debugPath, "vehicle.start_method") == 0) {
        StartMethod m{};
        if (!parseStartMethod(cmd.debugValue, &m)) return MqttCmd::CODE_BAD_REQUEST;
        if (base.vehicle.start_method != m) {
            base.vehicle.start_method = m;
            changed = true;
        }
    } else if (strcmp(cmd.debugPath, "vehicle.engine_detection_source") == 0) {
        if (strlen(cmd.debugValue) >= sizeof(base.vehicle.engine_detection_source)) return MqttCmd::CODE_BAD_REQUEST;
        if (strcmp(base.vehicle.engine_detection_source, cmd.debugValue) != 0) {
            strlcpy(base.vehicle.engine_detection_source, cmd.debugValue,
                    sizeof(base.vehicle.engine_detection_source));
            changed = true;
        }
    } else if (strcmp(cmd.debugPath, "vehicle.engine_input_id") == 0) {
        char* end = nullptr;
        unsigned long n = strtoul(cmd.debugValue, &end, 10);
        if (!end || *end || n > 255u) return MqttCmd::CODE_BAD_REQUEST;
        const uint8_t id = static_cast<uint8_t>(n);
        if (base.vehicle.engine_input_id != id) {
            base.vehicle.engine_input_id = id;
            changed = true;
        }
    } else if (strcmp(cmd.debugPath, "vehicle.start_input_id") == 0) {
        char* end = nullptr;
        unsigned long n = strtoul(cmd.debugValue, &end, 10);
        if (!end || *end || n > 255u) return MqttCmd::CODE_BAD_REQUEST;
        const uint8_t id = static_cast<uint8_t>(n);
        if (base.vehicle.start_input_id != id) {
            base.vehicle.start_input_id = id;
            changed = true;
        }
    } else {
        return MqttCmd::CODE_NOT_FOUND;
    }

    if (!changed) return MqttCmd::CODE_OK;
    if (!config.save()) return MqttCmd::CODE_INTERNAL;
    // Как SoftAP /config/save: RAM уже обновлён, нужен reboot для чистого старта менеджеров.
    core.setRebootRequired(true);
    core.getRelay().applyStarterConfig();
    core.getInputs().applyConfig();
    settleModemRxAfterFlash_();
    return MqttCmd::CODE_OK;
}

uint16_t MqttDebugSession::doReset_(const MqttCommand& cmd) {
    if (state_ != State::Idle) return MqttCmd::CODE_UNAVAILABLE;
    if (cmd.kind == MqttCommandKind::Cfg) {
        if (!config.reset()) return MqttCmd::CODE_INTERNAL;
        core.setRebootRequired(true);
        core.getRelay().applyStarterConfig();
        core.getInputs().applyConfig();
        settleModemRxAfterFlash_();
        return MqttCmd::CODE_OK;
    }
    if (cmd.kind == MqttCommandKind::Prg) {
        if (!config.resetPrograms()) return MqttCmd::CODE_INTERNAL;
        settleModemRxAfterFlash_();
        return MqttCmd::CODE_OK;
    }
    return MqttCmd::CODE_BAD_REQUEST;
}

uint16_t MqttDebugSession::doDelete_(const MqttCommand& cmd) {
    if (state_ != State::Idle) return MqttCmd::CODE_UNAVAILABLE;
    if (cmd.kind != MqttCommandKind::Prg || cmd.programId == 0) return MqttCmd::CODE_BAD_REQUEST;
    if (!config.deleteProgram(cmd.programId)) return MqttCmd::CODE_NOT_FOUND;
    settleModemRxAfterFlash_();
    return MqttCmd::CODE_OK;
}

uint16_t MqttDebugSession::doGet_(const MqttCommand& cmd, MqttClient& mqtt) {
    if (state_ != State::Idle) return MqttCmd::CODE_UNAVAILABLE;
    (void)mqtt;

    size_t len = 0;
    if (cmd.kind == MqttCommandKind::Cfg) {
        BufPrint bp(buf_, MqttDebugLimits::PUT_MAX - 1);
        config.emitBaseJson(bp);
        if (bp.overflow() || bp.length() == 0) return MqttCmd::CODE_UNPROCESSABLE;
        len = bp.length();
        strlcpy(sendTopicSuffix_, MqttTopics::CFG, sizeof(sendTopicSuffix_));
    } else if (cmd.kind == MqttCommandKind::Prg) {
        if (cmd.programId == 0) return MqttCmd::CODE_BAD_REQUEST;
        char path[BufferBytes::Fs::PROGRAM_PATH]{};
        program_json::formatProgramPath(path, sizeof(path), cmd.programId);
        size_t maxLen = MqttDebugLimits::PUT_MAX - 1;
        len = maxLen;
        if (!fileSystem.readFile(path, reinterpret_cast<char*>(buf_), len, maxLen) || len == 0) {
            return MqttCmd::CODE_NOT_FOUND;
        }
        strlcpy(sendTopicSuffix_, MqttTopics::PRG, sizeof(sendTopicSuffix_));
    } else {
        return MqttCmd::CODE_BAD_REQUEST;
    }

    sendCrc_ = mqttDebugCrc32(buf_, len);
    sendLen_ = static_cast<uint32_t>(len);
    sendOff_ = 0;
    sendN_ = static_cast<uint16_t>((len + kRawPerChunk - 1) / kRawPerChunk);
    if (sendN_ == 0) sendN_ = 1;
    sendI_ = 0;
    lastSendMs_ = 0;
    state_ = State::Sending;
    target_ = (cmd.kind == MqttCommandKind::Cfg) ? Target::Cfg : Target::Prg;
    return MqttCmd::CODE_OK;
}

uint16_t MqttDebugSession::handle(const MqttCommand& cmd, MqttClient& mqtt, SensorsController& sensors) {
    if (cmd.kind != MqttCommandKind::Cfg && cmd.kind != MqttCommandKind::Prg) {
        return MqttCmd::CODE_BAD_REQUEST;
    }

    const bool uploadStep = (cmd.debugOp == MqttDebugOp::Put || cmd.debugOp == MqttDebugOp::Commit ||
                             cmd.debugOp == MqttDebugOp::Abort);
    // New begin/get/set aborts an in-flight downlink so SoftAP-style mutate can start.
    if (state_ == State::Sending &&
        (cmd.debugOp == MqttDebugOp::Begin || cmd.debugOp == MqttDebugOp::Get ||
         cmd.debugOp == MqttDebugOp::Set || cmd.debugOp == MqttDebugOp::Reset ||
         cmd.debugOp == MqttDebugOp::Delete || cmd.debugOp == MqttDebugOp::Abort)) {
        abort();
    }
    if (state_ == State::CommitPending) return MqttCmd::CODE_UNAVAILABLE;
    if (busy() && !uploadStep) return MqttCmd::CODE_UNAVAILABLE;

    switch (cmd.debugOp) {
        case MqttDebugOp::Begin:
            return begin_(cmd, cmd.kind == MqttCommandKind::Cfg ? Target::Cfg : Target::Prg);
        case MqttDebugOp::Put:
            return put_(cmd);
        case MqttDebugOp::Commit: {
            const uint16_t code = commit_(sensors);
            if (state_ == State::CommitPending) {
                strlcpy(pendingReplyId_, cmd.id, sizeof(pendingReplyId_));
                return 0;
            }
            return code;
        }
        case MqttDebugOp::Abort:
            if (state_ == State::Receiving) {
                abort();
                return MqttCmd::CODE_OK;
            }
            return MqttCmd::CODE_CONFLICT;
        case MqttDebugOp::Get:
            return doGet_(cmd, mqtt);
        case MqttDebugOp::Set:
            if (cmd.kind != MqttCommandKind::Cfg) return MqttCmd::CODE_BAD_REQUEST;
            return doSet_(cmd);
        case MqttDebugOp::Reset:
            return doReset_(cmd);
        case MqttDebugOp::Delete:
            return doDelete_(cmd);
        default:
            return MqttCmd::CODE_BAD_REQUEST;
    }
}

void MqttDebugSession::tickDownlink(MqttClient& mqtt) {
    if (state_ != State::Sending) return;
    const uint32_t now = millis();
    if (lastSendMs_ != 0 && (now - lastSendMs_) < MqttDebugLimits::TX_PACE_MS) return;

    const size_t rem = sendLen_ - sendOff_;
    const size_t take = rem > kRawPerChunk ? kRawPerChunk : rem;

    char b64[MqttDebugLimits::CHUNK_B64_MAX + 1]{};
    if (take > 0 && mqttDebugBase64Encode(buf_ + sendOff_, take, b64, sizeof(b64)) == 0) {
        abort();
        return;
    }

    char payload[MqttDebugLimits::CHUNK_B64_MAX + 96]{};
    char crcHex[9]{};
    int n;
    if (sendI_ == 0) {
        mqttDebugU32ToHex(sendCrc_, crcHex);
        n = snprintf(payload, sizeof(payload),
                     "{\"t\":\"%s\",\"i\":%u,\"n\":%u,\"crc\":\"%s\",\"data\":\"%s\"}",
                     sendTopicSuffix_, (unsigned)sendI_, (unsigned)sendN_, crcHex, b64);
    } else {
        n = snprintf(payload, sizeof(payload),
                     "{\"t\":\"%s\",\"i\":%u,\"n\":%u,\"data\":\"%s\"}",
                     sendTopicSuffix_, (unsigned)sendI_, (unsigned)sendN_, b64);
    }
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(payload)) {
        abort();
        return;
    }
    if (!mqtt.publishDebugChunk(sendTopicSuffix_, payload, static_cast<uint16_t>(n))) {
        return;
    }
    sendOff_ += static_cast<uint32_t>(take);
    sendI_++;
    lastSendMs_ = now;
    if (sendI_ >= sendN_) {
        abort();
    }
}

void MqttDebugSession::tickFlashCommit(MqttClient& mqtt) {
    if (state_ != State::CommitPending) return;
    if (flashCommit.isPending()) return;

    const uint16_t code =
        flashCommit.getLastFlashOk() ? MqttCmd::CODE_OK : MqttCmd::CODE_UNPROCESSABLE;
    char id[MqttCmd::ID_MAX_LEN + 1]{};
    strlcpy(id, pendingReplyId_, sizeof(id));
    pendingReplyId_[0] = '\0';
    state_ = State::Idle;
    target_ = Target::None;

    if (id[0]) {
        logger.log(LogModule::MqttClient, "debug commit final id=%s code=%u\n", id, (unsigned)code);
        mqtt.publishDebugFinalReply(id, code);
    }
}

#endif // AUTOSTART_MQTT_DEBUG
