#pragma once

#include <stdint.h>

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
};

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

enum class MqttRunState : uint8_t {
    None = 0,
    Accepted,
    Finished,
    Failed,
};

struct MqttCommand {
    MqttCommandKind kind{MqttCommandKind::None};
    char id[17]{};
    uint8_t programId{0};
    MqttSetName setName{MqttSetName::None};
    uint16_t ref{0};
    bool enabled{false};
    bool hasEnabled{false};
};

inline const char* mqttCmdErrStr(MqttCmdErr e) {
    switch (e) {
        case MqttCmdErr::Parse: return "parse";
        case MqttCmdErr::Unknown: return "unknown";
        case MqttCmdErr::Args: return "args";
        case MqttCmdErr::NotFound: return "not_found";
        case MqttCmdErr::Rejected: return "rejected";
        case MqttCmdErr::Busy: return "busy";
        case MqttCmdErr::Conflict: return "conflict";
        default: return "";
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

inline const char* mqttRunStateStr(MqttRunState s) {
    switch (s) {
        case MqttRunState::Accepted: return "accepted";
        case MqttRunState::Finished: return "finished";
        case MqttRunState::Failed: return "failed";
        default: return "";
    }
}
