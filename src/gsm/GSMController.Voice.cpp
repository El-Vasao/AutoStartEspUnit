/**
 * @file GSMController.Voice.cpp
 * @brief Exclusive voice/SMS modem epoch: CALL_OWNER, SMS_OWNER, inbound DTMF program start.
 *
 * MQTT CIP is suspended for the whole epoch (CellularCore drain → StopTcp → AT → idle).
 */
#include "gsm/GSMController.h"

#include "config/Config.h"
#include "gsm/GsmAtParse.h"
#include "common/Logger.h"

#include <stdio.h>
#include <string.h>

void GSMController::voiceReset_() {
    _voiceKind = VoiceKind::None;
    _voicePhase = VoicePhase::Idle;
    _voicePhone[0] = '\0';
    _voiceSmsText[0] = '\0';
    _voiceDtmfBuf[0] = '\0';
    _voiceProgDigits[0] = '\0';
    _voiceDtmfLen = 0;
    _voiceProgDigitLen = 0;
    _voiceTimeoutMs = 0;
    _voiceEpochStartMs = 0;
    _voicePhaseDeadlineMs = 0;
    _voicePromptSeen = false;
    _voiceNoCarrier = false;
}

void GSMController::voiceFinish_(bool ok) {
    const VoiceKind kind = _voiceKind;
    logger.log("[GSMController] voice epoch end ok=%u kind=%u\n", (unsigned)ok, (unsigned)kind);
    if (kind == VoiceKind::OutCall || kind == VoiceKind::OutSms) {
        _voiceResultOk = ok;
        _voiceResultReady = true;
    }
    voiceReset_();
}

void GSMController::notifyMqttDrainedForServiceEpoch() {
    if (_voicePhase != VoicePhase::NeedMqttDrain) return;
    _voicePhase = VoicePhase::StopTcp;
    logger.log("[GSMController] voice: MQTT drained, stopping TCP\n");
}

bool GSMController::requestCallOwner(uint32_t timeoutMs) {
    if (_voicePhase != VoicePhase::Idle) return false;
    if (_state != GSMState::READY) return false;
    const char* phone = config.getBase().gsm.owner_phone;
    if (!phone || !phone[0]) return false;

    if (_timeStep != TimeStep::Idle) {
        timeFailStep_("voice_call");
    }

    strlcpy(_voicePhone, phone, sizeof(_voicePhone));
    _voiceSmsText[0] = '\0';
    _voiceTimeoutMs = timeoutMs ? timeoutMs : GSM::VOICE_DEFAULT_RING_MS;
    if (_voiceTimeoutMs > GSM::VOICE_CALL_CEILING_MS) {
        _voiceTimeoutMs = GSM::VOICE_CALL_CEILING_MS;
    }
    _voiceKind = VoiceKind::OutCall;
    _voicePhase = VoicePhase::NeedMqttDrain;
    _voiceEpochStartMs = millis();
    _voiceResultReady = false;
    _voiceResultOk = false;
    _voiceNoCarrier = false;
    logger.log("[GSMController] voice: request CALL_OWNER timeout=%lu\n",
               (unsigned long)_voiceTimeoutMs);
    return true;
}

bool GSMController::requestSmsOwner(const char* message) {
    if (_voicePhase != VoicePhase::Idle) return false;
    if (_state != GSMState::READY) return false;
    const char* phone = config.getBase().gsm.owner_phone;
    if (!phone || !phone[0]) return false;
    if (!message || !message[0]) return false;

    if (_timeStep != TimeStep::Idle) {
        timeFailStep_("voice_sms");
    }

    strlcpy(_voicePhone, phone, sizeof(_voicePhone));
    strlcpy(_voiceSmsText, message, sizeof(_voiceSmsText));
    _voiceTimeoutMs = GSM::SMS_CMGS_TIMEOUT_MS;
    _voiceKind = VoiceKind::OutSms;
    _voicePhase = VoicePhase::NeedMqttDrain;
    _voiceEpochStartMs = millis();
    _voiceResultReady = false;
    _voiceResultOk = false;
    _voicePromptSeen = false;
    logger.log("[GSMController] voice: request SMS_OWNER\n");
    return true;
}

bool GSMController::takeVoiceOpResult(bool& okOut) {
    if (!_voiceResultReady) return false;
    okOut = _voiceResultOk;
    _voiceResultReady = false;
    return true;
}

bool GSMController::takePendingProgramStart(uint8_t& programIdOut) {
    if (!_voicePendingProgramValid) return false;
    programIdOut = _voicePendingProgramId;
    _voicePendingProgramValid = false;
    _voicePendingProgramId = 0;
    return true;
}

void GSMController::voiceAbortInboundClipWait_() {
    _inboundRingSeen = false;
    _inboundClipDeadlineMs = 0;
    _inboundClipDigits[0] = '\0';
}

bool GSMController::voiceTryBeginInbound_(const char* clipDigits) {
    if (_voicePhase != VoicePhase::Idle) return false;
    if (_state != GSMState::READY) return false;

    const auto& gsmCfg = config.getBase().gsm;
    if (!gsmCfg.dtmf_password[0]) return false;
    if (!gsmCfg.owner_phone[0]) return false;
    if (!clipDigits || !clipDigits[0]) return false;
    if (!gsm_at::phonesMatch(gsmCfg.owner_phone, clipDigits)) {
        logger.log("[GSMController] voice: CLIP reject (not owner)\n");
        return false;
    }

    if (_timeStep != TimeStep::Idle) {
        timeFailStep_("voice_inbound");
    }

    strlcpy(_voicePhone, gsmCfg.owner_phone, sizeof(_voicePhone));
    _voiceDtmfBuf[0] = '\0';
    _voiceProgDigits[0] = '\0';
    _voiceDtmfLen = 0;
    _voiceProgDigitLen = 0;
    _voiceKind = VoiceKind::Inbound;
    _voicePhase = VoicePhase::NeedMqttDrain;
    _voiceEpochStartMs = millis();
    _voiceTimeoutMs = GSM::VOICE_CALL_CEILING_MS;
    _voiceNoCarrier = false;
    voiceAbortInboundClipWait_();
    logger.log("[GSMController] voice: inbound owner match, starting epoch\n");
    return true;
}

void GSMController::voiceOnDtmfTone_(char tone) {
    if (_voicePhase != VoicePhase::CollectPassword && _voicePhase != VoicePhase::CollectProgId) {
        return;
    }

    const auto& gsmCfg = config.getBase().gsm;
    const uint32_t now = millis();

    if (_voicePhase == VoicePhase::CollectPassword) {
        if (tone == '#' || tone == '*') {
            _voicePhase = VoicePhase::HangupSend;
            _voiceResultOk = false;
            return;
        }
        if (tone < '0' || tone > '9') return;
        if (_voiceDtmfLen + 1 >= sizeof(_voiceDtmfBuf)) {
            _voicePhase = VoicePhase::HangupSend;
            return;
        }
        _voiceDtmfBuf[_voiceDtmfLen++] = tone;
        _voiceDtmfBuf[_voiceDtmfLen] = '\0';

        const size_t pwLen = strlen(gsmCfg.dtmf_password);
        if (_voiceDtmfLen < pwLen) return;
        if (strcmp(_voiceDtmfBuf, gsmCfg.dtmf_password) != 0) {
            logger.log("[GSMController] voice: DTMF password mismatch\n");
            _voicePhase = VoicePhase::HangupSend;
            return;
        }
        logger.log("[GSMController] voice: DTMF password OK, await program id\n");
        _voiceProgDigitLen = 0;
        _voiceProgDigits[0] = '\0';
        _voicePhase = VoicePhase::CollectProgId;
        _voicePhaseDeadlineMs = now + GSM::VOICE_PROGID_TIMEOUT_MS;
        return;
    }

    // CollectProgId
    if (tone == '*') {
        _voicePhase = VoicePhase::HangupSend;
        return;
    }
    if (tone == '#') {
        if (_voiceProgDigitLen == 0) {
            _voicePhase = VoicePhase::HangupSend;
            return;
        }
        unsigned long id = 0;
        for (uint8_t i = 0; i < _voiceProgDigitLen; i++) {
            id = id * 10UL + (unsigned long)(_voiceProgDigits[i] - '0');
        }
        if (id == 0 || id > 255UL) {
            logger.log("[GSMController] voice: bad program id\n");
            _voicePhase = VoicePhase::HangupSend;
            return;
        }
        _voicePendingProgramId = (uint8_t)id;
        _voicePendingProgramValid = true;
        logger.log("[GSMController] voice: inbound start program %u\n", (unsigned)id);
        _voicePhase = VoicePhase::HangupSend;
        _voiceResultOk = true; // hangup still runs; FinishOk after ATH
        return;
    }
    if (tone < '0' || tone > '9') return;
    if (_voiceProgDigitLen >= 3) {
        _voicePhase = VoicePhase::HangupSend;
        return;
    }
    _voiceProgDigits[_voiceProgDigitLen++] = tone;
    _voiceProgDigits[_voiceProgDigitLen] = '\0';
}

void GSMController::voiceHandleUrc_(const char* line) {
    if (!line || !line[0]) return;

    if (strcmp(line, "RING") == 0 || strncmp(line, "+CRING:", 7) == 0) {
        if (_voicePhase == VoicePhase::Idle && _state == GSMState::READY) {
            _inboundRingSeen = true;
            _inboundClipDeadlineMs = millis() + GSM::VOICE_CLIP_WAIT_MS;
            if (_inboundClipDigits[0]) {
                (void)voiceTryBeginInbound_(_inboundClipDigits);
            }
        }
        return;
    }

    if (strncmp(line, "+CLIP:", 6) == 0) {
        char digits[24];
        if (gsm_at::parseClipDigits(line, digits, sizeof(digits))) {
            strlcpy(_inboundClipDigits, digits, sizeof(_inboundClipDigits));
            if (_voicePhase == VoicePhase::Idle) {
                if (!_inboundRingSeen) {
                    _inboundRingSeen = true;
                    _inboundClipDeadlineMs = millis() + GSM::VOICE_CLIP_WAIT_MS;
                }
                (void)voiceTryBeginInbound_(digits);
            }
        }
        return;
    }

    char tone = 0;
    if (gsm_at::parseDtmfTone(line, tone)) {
        voiceOnDtmfTone_(tone);
        return;
    }

    if (strcmp(line, "NO CARRIER") == 0 || strcmp(line, "BUSY") == 0 ||
        strcmp(line, "NO ANSWER") == 0) {
        _voiceNoCarrier = true;
        if (_voicePhase == VoicePhase::DialRing || _voicePhase == VoicePhase::CollectPassword ||
            _voicePhase == VoicePhase::CollectProgId || _voicePhase == VoicePhase::AnswerWait) {
            if (_voicePhase == VoicePhase::DialRing) {
                // Outbound: remote hung up / no answer — treat as completed ring attempt.
                _voicePhase = VoicePhase::FinishOk;
            } else {
                _voicePhase = VoicePhase::FinishFail;
            }
        }
    }
}

void GSMController::serviceVoice_(uint32_t now) {
    if (_voicePhase == VoicePhase::Idle) {
        if (_inboundRingSeen && _inboundClipDeadlineMs != 0 &&
            (int32_t)(now - _inboundClipDeadlineMs) >= 0) {
            logger.log("[GSMController] voice: CLIP wait timeout\n");
            voiceAbortInboundClipWait_();
        }
        return;
    }

    if ((int32_t)(now - _voiceEpochStartMs) >= (int32_t)GSM::VOICE_CALL_CEILING_MS) {
        logger.log("[GSMController] voice: epoch ceiling\n");
        if (_voicePhase != VoicePhase::HangupSend && _voicePhase != VoicePhase::HangupWait &&
            _voicePhase != VoicePhase::FinishOk && _voicePhase != VoicePhase::FinishFail) {
            _voicePhase = VoicePhase::HangupSend;
        }
    }

    switch (_voicePhase) {
    case VoicePhase::NeedMqttDrain:
        // CellularCore drains MQTT and calls notifyMqttDrainedForServiceEpoch().
        return;

    case VoicePhase::StopTcp: {
        if (_stack.tcp.isConnected() || _stack.tcp.isConnecting() || _stack.tcp.isBusBusy()) {
            _stack.tcp.stop("voice");
        }
        if (_stack.tcp.isBusBusy() || _stack.at.isBusy() || _stack.at.hasResult()) return;
        if (_voiceKind == VoiceKind::OutCall) {
            _voicePhase = VoicePhase::DialSend;
        } else if (_voiceKind == VoiceKind::OutSms) {
            _voicePhase = VoicePhase::CmgsSend;
        } else {
            _voicePhase = VoicePhase::AnswerSend;
        }
        return;
    }

    case VoicePhase::DialSend: {
        // Voice call: trailing ';' required on SIM800.
        char cmd[48];
        snprintf(cmd, sizeof(cmd), "ATD%s;", _voicePhone);
        if (!sendAt(cmd, "ATD", AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS)) return;
        _voicePhase = VoicePhase::DialWaitOk;
        return;
    }

    case VoicePhase::DialWaitOk:
        if (_awaitError || awaitTimedOut(now)) {
            resetAwait();
            _voicePhase = VoicePhase::FinishFail;
            return;
        }
        if (!_awaitOk) return;
        resetAwait();
        _voicePhase = VoicePhase::DialRing;
        _voicePhaseDeadlineMs = now + _voiceTimeoutMs;
        _voiceNoCarrier = false;
        return;

    case VoicePhase::DialRing:
        if (_voiceNoCarrier || (int32_t)(now - _voicePhaseDeadlineMs) >= 0) {
            _voicePhase = VoicePhase::HangupSend;
        }
        return;

    case VoicePhase::HangupSend:
        if (_stack.at.isBusy() || _stack.at.hasResult()) return;
        if (!sendAt("ATH", "ATH", AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS)) return;
        _voicePhase = VoicePhase::HangupWait;
        return;

    case VoicePhase::HangupWait:
        if (_awaitOk || _awaitError || awaitTimedOut(now)) {
            resetAwait();
            if (_voiceKind == VoiceKind::Inbound && _voicePendingProgramValid) {
                _voicePhase = VoicePhase::FinishOk;
            } else if (_voiceKind == VoiceKind::OutCall) {
                _voicePhase = VoicePhase::FinishOk;
            } else if (_voiceKind == VoiceKind::OutSms) {
                _voicePhase = _voiceResultOk ? VoicePhase::FinishOk : VoicePhase::FinishFail;
            } else {
                _voicePhase = VoicePhase::FinishFail;
            }
        }
        return;

    case VoicePhase::CmgsSend: {
        char cmd[48];
        snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", _voicePhone);
        _cmdId++;
        snprintf(_atCmdBuf, sizeof(_atCmdBuf), "%s", cmd);
        AtSession::Request r{_atCmdBuf, GSM::SMS_CMGS_TIMEOUT_MS,
                             atExpectMask(AtSession::Expect::Prompt), nullptr, "CMGS"};
        if (!_stack.at.enqueue(r)) return;
        resetAwait();
        _lastCommandTime = now;
        _voicePromptSeen = false;
        _voicePhaseDeadlineMs = now + GSM::SMS_CMGS_TIMEOUT_MS;
        _voicePhase = VoicePhase::CmgsWaitPrompt;
        return;
    }

    case VoicePhase::CmgsWaitPrompt:
        if (_voicePromptSeen) {
            _voicePhase = VoicePhase::CmgsSendBody;
            return;
        }
        if (_awaitError || (int32_t)(now - _voicePhaseDeadlineMs) >= 0) {
            resetAwait();
            _voicePhase = VoicePhase::FinishFail;
        }
        return;

    case VoicePhase::CmgsSendBody: {
        _stack.uart.writeRaw(_voiceSmsText);
        _stack.uart.writeByte(0x1A);
        beginAwait(AwaitKind::OK, GSM::SMS_CMGS_TIMEOUT_MS);
        _voicePhase = VoicePhase::CmgsWaitOk;
        return;
    }

    case VoicePhase::CmgsWaitOk:
        if (_awaitError || awaitTimedOut(now)) {
            resetAwait();
            _voicePhase = VoicePhase::FinishFail;
            return;
        }
        if (!_awaitOk) return;
        resetAwait();
        _voicePhase = VoicePhase::FinishOk;
        return;

    case VoicePhase::AnswerSend:
        if (_stack.at.isBusy() || _stack.at.hasResult()) return;
        if (!sendAt("ATA", "ATA", AwaitKind::OK, GSM::AT_OK_TIMEOUT_MS)) return;
        _voicePhase = VoicePhase::AnswerWait;
        return;

    case VoicePhase::AnswerWait:
        if (_awaitError || awaitTimedOut(now) || _voiceNoCarrier) {
            resetAwait();
            _voicePhase = VoicePhase::FinishFail;
            return;
        }
        if (!_awaitOk) return;
        resetAwait();
        _voiceDtmfLen = 0;
        _voiceDtmfBuf[0] = '\0';
        _voicePhase = VoicePhase::CollectPassword;
        _voicePhaseDeadlineMs = now + GSM::VOICE_PASSWORD_TIMEOUT_MS;
        return;

    case VoicePhase::CollectPassword:
        if (_voiceNoCarrier || (int32_t)(now - _voicePhaseDeadlineMs) >= 0) {
            logger.log("[GSMController] voice: password timeout\n");
            _voicePhase = VoicePhase::HangupSend;
        }
        return;

    case VoicePhase::CollectProgId:
        if (_voiceNoCarrier || (int32_t)(now - _voicePhaseDeadlineMs) >= 0) {
            logger.log("[GSMController] voice: program id timeout\n");
            _voicePendingProgramValid = false;
            _voicePhase = VoicePhase::HangupSend;
        }
        return;

    case VoicePhase::FinishOk:
        voiceFinish_(true);
        return;

    case VoicePhase::FinishFail:
        voiceFinish_(false);
        return;

    case VoicePhase::Idle:
    default:
        return;
    }
}
