#include <unity.h>

#include <string.h>

#include "Arduino.h"
#include "HostFakes.h"
#include "LogSink.h"
#include "config/Config.h"
#include "core/Core.h"
#include "gsm/GsmController.h"
#include "modem/AtSession.h"
#include "program/ProgramExecutor.h"

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

    static void rx(GsmController& gsm, const char* line) { gsm.onRxLine(line); }

    static void armCollectPassword(GsmController& gsm) {
        gsm._state = GsmState::READY;
        gsm._voiceKind = GsmController::VoiceKind::Inbound;
        gsm._voicePhase = GsmController::VoicePhase::CollectPassword;
        gsm._voiceEpochStartMs = millis();
        gsm._voiceDtmfLen = 0;
        gsm._voiceDtmfBuf[0] = '\0';
        gsm._voiceProgDigitLen = 0;
        gsm._voiceProgDigits[0] = '\0';
        gsm._voicePendingProgramValid = false;
    }

    static void armReady(GsmController& gsm, bool tcpConnected) {
        gsm._state = GsmState::READY;
        gsm._stack.tcp.testSetConnected(tcpConnected);
    }

    static bool pumpOk(GsmController& gsm) {
        gsm._stack.at.tick(millis());
        if (!gsm._stack.at.hasResult()) gsm._stack.at.onLine("OK");
        if (!gsm._stack.at.hasResult()) return false;
        const AtSession::Result r = gsm._stack.at.takeResult();
        (void)gsm._stack.tcp.consumeAtResult(r);
        gsm.onRxLine("OK");
        return true;
    }

    static void tickAt(GsmController& gsm) { gsm._stack.at.tick(millis()); }

    static GsmController::VoicePhase phase(const GsmController& gsm) { return gsm._voicePhase; }

    static bool pendingValid(const GsmController& gsm) { return gsm._voicePendingProgramValid; }
};

void setUp() {
    test_clock::set(10000);
    test_log::clear();
    Serial.clearTx();
    config.testBase().gsm.owner_phone[0] = '\0';
    config.testBase().gsm.dtmf_password[0] = '\0';
    ExecutorTestProbe::reset();
    ExecutorTestProbe::intercept = false;
    ProgramExecutorTestFlags::set(false, false);
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

static void test_ring_during_mqtt_drains_cip_before_answer() {
    strlcpy(config.testBase().gsm.owner_phone, "+79991234567", sizeof(config.testBase().gsm.owner_phone));
    strlcpy(config.testBase().gsm.dtmf_password, "12", sizeof(config.testBase().gsm.dtmf_password));

    GsmController gsm;
    GsmVoiceTestAccess::armReady(gsm, true);

    GsmVoiceTestAccess::rx(gsm, "RING");
    GsmVoiceTestAccess::rx(gsm, "+CLIP: \"+79991234567\",145");
    TEST_ASSERT_EQUAL_INT((int)GsmController::VoicePhase::NeedMqttDrain, (int)GsmVoiceTestAccess::phase(gsm));

    GsmVoiceTestAccess::service(gsm);
    TEST_ASSERT_EQUAL_INT((int)GsmController::VoicePhase::NeedMqttDrain, (int)GsmVoiceTestAccess::phase(gsm));
    TEST_ASSERT_NULL(strstr(Serial.tx(), "ATA"));
    TEST_ASSERT_NULL(strstr(Serial.tx(), "CIPCLOSE"));

    gsm.notifyMqttDrainedForServiceEpoch();
    GsmVoiceTestAccess::service(gsm);
    TEST_ASSERT_EQUAL_INT((int)GsmController::VoicePhase::StopTcp, (int)GsmVoiceTestAccess::phase(gsm));
    TEST_ASSERT_NULL(strstr(Serial.tx(), "ATA"));

    TEST_ASSERT_TRUE(GsmVoiceTestAccess::pumpOk(gsm));
    TEST_ASSERT_NOT_NULL(strstr(Serial.tx(), "AT+CIPCLOSE"));
    TEST_ASSERT_NULL(strstr(Serial.tx(), "ATA"));

    Serial.clearTx();
    GsmVoiceTestAccess::service(gsm);
    TEST_ASSERT_EQUAL_INT((int)GsmController::VoicePhase::AnswerSend, (int)GsmVoiceTestAccess::phase(gsm));
    GsmVoiceTestAccess::service(gsm);
    GsmVoiceTestAccess::tickAt(gsm);
    TEST_ASSERT_NOT_NULL(strstr(Serial.tx(), "ATA"));
    TEST_ASSERT_EQUAL_INT((int)GsmController::VoicePhase::AnswerWait, (int)GsmVoiceTestAccess::phase(gsm));
}

static void test_dtmf_during_starter_step_is_taken_for_start_same_tick() {
    strlcpy(config.testBase().gsm.dtmf_password, "12", sizeof(config.testBase().gsm.dtmf_password));
    GsmController gsm;
    GsmVoiceTestAccess::armCollectPassword(gsm);
    ProgramExecutor& running = core.getProgramExecutor();
    ProgramExecutorTestFlags::set(running, 1, true, true);
    TEST_ASSERT_TRUE(running.isInStarterStep());

    GsmVoiceTestAccess::rx(gsm, "+DTMF: 1");
    uint8_t programId = 0;
    TEST_ASSERT_FALSE(gsm.takePendingProgramStart(programId));
    GsmVoiceTestAccess::rx(gsm, "+DTMF: 2");
    GsmVoiceTestAccess::rx(gsm, "+DTMF: 4");
    GsmVoiceTestAccess::rx(gsm, "+DTMF: #");

    TEST_ASSERT_TRUE(gsm.takePendingProgramStart(programId));
    TEST_ASSERT_EQUAL_UINT8(4, programId);
    TEST_ASSERT_EQUAL_INT((int)GsmController::VoicePhase::HangupSend, (int)GsmVoiceTestAccess::phase(gsm));
    // Тот же тик: Core вызывает start() поверх ENGINE_START; здесь считаем вызов, без загрузки JSON.
    ExecutorTestProbe::intercept = true;
    TEST_ASSERT_TRUE(running.start(programId));
    TEST_ASSERT_EQUAL_UINT8(4, ExecutorTestProbe::lastStartId);
    TEST_ASSERT_EQUAL_INT(1, ExecutorTestProbe::starts);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_hangup_send_deadline_does_not_start_program);
    RUN_TEST(test_sms_wait_ok_ignores_voice_epoch_ceiling);
    RUN_TEST(test_ring_during_mqtt_drains_cip_before_answer);
    RUN_TEST(test_dtmf_during_starter_step_is_taken_for_start_same_tick);
    return UNITY_END();
}
