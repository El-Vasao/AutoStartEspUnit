#include "gsm/GsmInitPhase.h"

const char* gsmInitPhaseName(GsmInitPhase p) {
    switch (p) {
    case GsmInitPhase::None: return "None";
    case GsmInitPhase::HypSendAt: return "HypSendAt";
    case GsmInitPhase::HypAwaitAt: return "HypAwaitAt";
    case GsmInitPhase::HypSendCgmi: return "HypSendCgmi";
    case GsmInitPhase::HypAwaitCgmi: return "HypAwaitCgmi";
    case GsmInitPhase::PreCfunSend: return "PreCfunSend";
    case GsmInitPhase::PreCfunQuiet: return "PreCfunQuiet";
    case GsmInitPhase::BsCooldown: return "BsCooldown";
    case GsmInitPhase::BsSettle: return "BsSettle";
    case GsmInitPhase::BsSendAt: return "BsSendAt";
    case GsmInitPhase::BsAwaitAt: return "BsAwaitAt";
    case GsmInitPhase::BsSendCgmi: return "BsSendCgmi";
    case GsmInitPhase::BsAwaitCgmi: return "BsAwaitCgmi";
    case GsmInitPhase::EarlyAteSend: return "EarlyAteSend";
    case GsmInitPhase::EarlyAteWait: return "EarlyAteWait";
    case GsmInitPhase::DIprQ: return "DIprQ";
    case GsmInitPhase::DIprWait: return "DIprWait";
    case GsmInitPhase::DLockSendIpr: return "DLockSendIpr";
    case GsmInitPhase::DLockWaitIpr: return "DLockWaitIpr";
    case GsmInitPhase::DLockSendW: return "DLockSendW";
    case GsmInitPhase::DLockWaitW: return "DLockWaitW";
    case GsmInitPhase::DLockVerifyAt: return "DLockVerifyAt";
    case GsmInitPhase::DLockAwaitVerifyAt: return "DLockAwaitVerifyAt";
    case GsmInitPhase::CCreg: return "CCreg";
    case GsmInitPhase::CCregWait: return "CCregWait";
    case GsmInitPhase::CCgatt: return "CCgatt";
    case GsmInitPhase::CCgattWait: return "CCgattWait";
    case GsmInitPhase::CCgattBackoff: return "CCgattBackoff";
    case GsmInitPhase::CSapbr: return "CSapbr";
    case GsmInitPhase::CSapbrWait: return "CSapbrWait";
    case GsmInitPhase::ModAte: return "ModAte";
    case GsmInitPhase::ModCmee: return "ModCmee";
    case GsmInitPhase::ModClts: return "ModClts";
    case GsmInitPhase::ModCreg2: return "ModCreg2";
    }
    return "?";
}
