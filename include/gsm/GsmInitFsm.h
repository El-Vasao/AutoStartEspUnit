#pragma once

class GSMController;

/**
 * INIT sub-FSM (baud hypothesis, PreCfun, baud search, IPR NV, resume CREG/CGATT/SAPBR, modem policy).
 * State fields still live on `GSMController` for bring-up compatibility; this type owns the tick logic TU.
 */
class GsmInitFsm {
public:
    static void tick(GSMController& gsm);

    static constexpr uint8_t kBaudCandidateCount = 5;
    static uint32_t baudCandidateAt(uint8_t idx);
};
