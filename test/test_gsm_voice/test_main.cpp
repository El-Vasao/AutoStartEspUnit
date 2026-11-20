#include <unity.h>

#include <string.h>

#include "Arduino.h"
#include "LogSink.h"
#include "gsm/GsmController.h"
#include "modem/AtSession.h"

struct GsmVoiceTestAccess {
    static void armInboundHangup(GsmController& gsm, uint8_t programId) {
        gsm._state = GsmState::READY;
        gsm._voiceKind = GsmController::VoiceKind::Inbound;
        gsm._voicePhase = GsmController::VoicePhase::HangupSend;
        gsm._voicePendingProgramId = programId;
        gsm._voicePendingProgramValid = true;
        gsm._voiceResultOk = true;
        gsm._voiceEpochStartMs = millis();
        gsm._voiceHangupDeadlineMs = 0;
    }

    static void armSmsWaitOk(GsmController& gsm) {
        gsm._state = GsmState::READY;
        gsm._voiceKind = GsmController::VoiceKind::OutSms;
        gsm._voicePhase = GsmController::VoicePhase::CmgsWaitOk;
        // Эпоха уже дольше голосового потолка; await submit ещё жив.
        gsm._voiceEpochStartMs = millis() - Gsm::VOICE_CALL_CEILING_MS - 1000;
        gsm._voiceResultReady = false;
        gsm._voiceResultOk = false;
        gsm.beginAwait(GsmController::AwaitKind::OK, Gsm::SMS_CMGS_SUBMIT_TIMEOUT_MS);
    }

    static bool holdAtBusy(GsmController& gsm) {
        AtSession::Request req{};
        req.cmd = "AT";
        req.timeoutMs = 60000;
        req.expectMask = (uint8_t)AtSession::Expect::Ok;
        if (!gsm._stack.at.enqueue(req)) return false;
        gsm._stack.at.tick(millis());
        return gsm._stack.at.isBusy();
    }

    static void service(GsmController& gsm) { gsm.serviceVoice_(millis()); }

    static GsmController::VoicePhase phase(const GsmController& gsm) { return gsm._voicePhase; }

    static bool pendingValid(const GsmController& gsm) { return gsm._voicePendingProgramValid; }
};

void setUp() {
    test_clock::set(10000);
    test_log::clear();
    Serial.clearTx();
}

void tearDown() {}

static void test_hangup_send_deadline_does_not_start_program() {
    GsmController gsm;
    GsmVoiceTestAccess::armInboundHangup(gsm, 4);
    TEST_ASSERT_TRUE(GsmVoiceTestAccess::holdAtBusy(gsm));

    GsmVoiceTestAccess::service(gsm);
    TEST_ASSERT_EQUAL_INT((int)GsmController::VoicePhase::HangupSend, (int)GsmVoiceTestAccess::phase(gsm));
    TEST_ASSERT_TRUE(GsmVoiceTestAccess::pendingValid(gsm));
    TEST_ASSERT_NULL(strstr(Serial.tx(), "ATH"));

    test_clock::advance(Gsm::VOICE_HANGUP_TIMEOUT_MS);
    GsmVoiceTestAccess::service(gsm);

    uint8_t programId = 99;
    TEST_ASSERT_FALSE(gsm.takePendingProgramStart(programId));
    TEST_ASSERT_EQUAL_UINT8(99, programId);
    TEST_ASSERT_FALSE(GsmVoiceTestAccess::pendingValid(gsm));
    TEST_ASSERT_EQUAL_INT((int)GsmController::VoicePhase::FinishFail, (int)GsmVoiceTestAccess::phase(gsm));
    TEST_ASSERT_TRUE((int)GsmVoiceTestAccess::phase(gsm) != (int)GsmController::VoicePhase::FinishOk);
    TEST_ASSERT_NULL(strstr(Serial.tx(), "ATH"));
}

static void test_sms_wait_ok_ignores_voice_epoch_ceiling() {
    GsmController gsm;
    GsmVoiceTestAccess::armSmsWaitOk(gsm);

    GsmVoiceTestAccess::service(gsm);

    TEST_ASSERT_EQUAL_INT((int)GsmController::VoicePhase::CmgsWaitOk,
                          (int)GsmVoiceTestAccess::phase(gsm));
    TEST_ASSERT_FALSE(test_log::contains("voice: epoch ceiling"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_hangup_send_deadline_does_not_start_program);
    RUN_TEST(test_sms_wait_ok_ignores_voice_epoch_ceiling);
    return UNITY_END();
}
