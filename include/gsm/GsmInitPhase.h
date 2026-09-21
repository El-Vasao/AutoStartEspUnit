#pragma once

#include <stdint.h>

/// Подфазы INIT (числовые значения совместимы с прежним uint8_t).
enum class GsmInitPhase : uint8_t {
    None = 0,
    HypSendAt = 1,
    HypAwaitAt = 2,
    HypSendCgmi = 3,
    HypAwaitCgmi = 4,

    /// Best-effort soft-reboot на UART_BAUD перед baud search.
    PreCfunSend = 5,
    PreCfunQuiet = 6,

    BsCooldown = 10,
    BsSettle = 11,
    BsSendAt = 12,
    BsAwaitAt = 13,
    BsSendCgmi = 14,
    BsAwaitCgmi = 15,

    EarlyAteSend = 16,
    EarlyAteWait = 17,

    DIprQ = 20,
    DIprWait = 21,

    DLockSendIpr = 22,
    DLockWaitIpr = 23,
    DLockSendW = 24,
    DLockWaitW = 25,
    DLockVerifyAt = 26,
    DLockAwaitVerifyAt = 27,

    CCreg = 30,
    CCregWait = 31,
    CCgatt = 32,
    CCgattWait = 33,
    CSapbr = 34,
    CSapbrWait = 35,
    /// Пауза перед повтором `AT+CGATT?` после SIM busy / ошибки (см. GSM::INIT_CGATT_*).
    CCgattBackoff = 36,

    ModAte = 40,
    ModCmee = 41,
    ModCreg2 = 42,
};

const char* gsmInitPhaseName(GsmInitPhase p);
