#pragma once

#include <stdint.h>

#include "common/Constants.h"

enum class MqttCommandKind : uint8_t {
    None = 0,
    Run,
    Stop,
    List,
    Status,
    Set,
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

/// Internal classification; mapped to HTTP-like MqttCmd::CODE_* on the wire.
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
        case MqttCommandKind::Set: return "set";
        default: return "";
    }
}
