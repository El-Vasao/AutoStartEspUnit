#pragma once

#include <stdint.h>

#include "common/Constants.h"

enum class MqttCommandKind : uint8_t {
    None = 0,
    Run,
    Stop,
    List,
    Status,
    Seq,
    Set,
    SetTime,
    HwMap,
    TrigMap,
    Reboot,
    Cfg,
    Prg,
    ModemReboot,
};

enum class MqttSetName : uint8_t {
    None = 0,
    Thermostat,
    BatterySaver,
    Input,
    Trigger,
    TempTrigger,
    WifiAp,
};

enum class MqttDebugOp : uint8_t {
    None = 0,
    Begin,
    Put,
    Commit,
    Abort,
    Get,
    Set,
    Reset,
    Delete,
};

/// Внутренняя классификация; на линии отображается в MqttCmd::CODE_* по образцу HTTP.
enum class MqttCmdErr : uint8_t {
    None = 0,
    Parse,
    Unknown,
    Args,
    NotFound,
    Rejected,
    Busy,
    Conflict,
};

struct MqttCommand {
    MqttCommandKind kind{MqttCommandKind::None};
    char id[MqttCmd::ID_MAX_LEN + 1]{};
    uint8_t programId{0};
    MqttSetName setName{MqttSetName::None};
    uint16_t ref{0};
    bool enabled{false};
    bool hasEnabled{false};
    uint32_t epoch{0};
    bool hasEpoch{false};

    MqttDebugOp debugOp{MqttDebugOp::None};
    uint32_t debugBytes{0};
    bool hasDebugBytes{false};
    char debugCrcHex[9]{};
    uint16_t debugChunkI{0};
    uint16_t debugChunkN{0};
    bool hasDebugChunk{false};
    char debugPath[48]{};
    char debugValue[48]{};
    /// Для `put`: сырой JSON команды живёт только до dispatch; data читается повторным сканом.
};

inline uint16_t mqttErrToHttpCode(MqttCmdErr e) {
    switch (e) {
        case MqttCmdErr::None: return MqttCmd::CODE_OK;
        case MqttCmdErr::Parse:
        case MqttCmdErr::Unknown:
        case MqttCmdErr::Args: return MqttCmd::CODE_BAD_REQUEST;
        case MqttCmdErr::NotFound: return MqttCmd::CODE_NOT_FOUND;
        case MqttCmdErr::Conflict: return MqttCmd::CODE_CONFLICT;
        case MqttCmdErr::Rejected: return MqttCmd::CODE_UNPROCESSABLE;
        case MqttCmdErr::Busy: return MqttCmd::CODE_UNAVAILABLE;
        default: return MqttCmd::CODE_BAD_REQUEST;
    }
}

inline const char* mqttCmdKindStr(MqttCommandKind k) {
    switch (k) {
        case MqttCommandKind::Run: return "run";
        case MqttCommandKind::Stop: return "stop";
        case MqttCommandKind::List: return "list";
        case MqttCommandKind::Status: return "status";
        case MqttCommandKind::Seq: return "seq";
        case MqttCommandKind::Set: return "set";
        case MqttCommandKind::SetTime: return "set_time";
        case MqttCommandKind::HwMap: return "hwmap";
        case MqttCommandKind::TrigMap: return "trigmap";
        case MqttCommandKind::Reboot: return "reboot";
        case MqttCommandKind::Cfg: return "cfg";
        case MqttCommandKind::Prg: return "prg";
        case MqttCommandKind::ModemReboot: return "modem_reboot";
        default: return "";
    }
}

inline bool mqttCmdIsDebugKind(MqttCommandKind k) {
    return k == MqttCommandKind::Cfg || k == MqttCommandKind::Prg || k == MqttCommandKind::ModemReboot;
}
