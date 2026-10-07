#pragma once

#include "common/Constants.h"

class GSMController;

/**
 * INIT sub-FSM (baud hypothesis, PreCfun, baud search, IPR NV, resume CREG/CGATT/SAPBR, modem policy).
 * State fields still live on `GSMController` for bring-up compatibility; this type owns the tick logic TU.
 */
class GsmInitFsm {
public:
    static void tick(GSMController& gsm);

    static constexpr uint8_t kBaudCandidateCount = GSM::UART_BAUD_CANDIDATE_COUNT;
};
