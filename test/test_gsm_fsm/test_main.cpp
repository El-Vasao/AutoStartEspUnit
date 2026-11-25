#include <unity.h>

#include <string.h>

#include "Arduino.h"
#include "LogSink.h"
#include "config/Config.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "gsm/GsmController.h"
#include "gsm/GsmInitPhase.h"

struct GsmFsmTestAccess {
    static void init(GsmController& gsm) { gsm.handleInit(); }
    static void reg(GsmController& gsm) { gsm.handleRegistering(); }
    static void gprs(GsmController& gsm) { gsm.handleGprsSetup(); }
    static void attach(GsmController& gsm) { gsm.handleGprsAttach(); }
    static void rx(GsmController& gsm, const char* line) { gsm.onRxLine(line); }

    static void flushTx(GsmController& gsm) { gsm._stack.at.tick(millis()); }

    static void acceptOk(GsmController& gsm, const char* prefix = nullptr) {
        gsm._stack.at.tick(millis());
        if (prefix) {
            gsm._stack.at.onLine(prefix);
            gsm.onRxLine(prefix);
        }
        if (!gsm._stack.at.hasResult()) gsm._stack.at.onLine("OK");
        if (gsm._stack.at.hasResult()) {
            const AtSession::Result r = gsm._stack.at.takeResult();
            (void)gsm._stack.tcp.consumeAtResult(r);
        }
        gsm.onRxLine("OK");
    }

    static void dropTimedOutAt(GsmController& gsm) {
        gsm._stack.at.tick(millis());
        if (gsm._stack.at.hasResult()) {
            const AtSession::Result r = gsm._stack.at.takeResult();
            (void)gsm._stack.tcp.consumeAtResult(r);
        }
    }
    static GsmState state(const GsmController& gsm) { return gsm._state; }
    static GsmInitPhase phase(const GsmController& gsm) { return gsm._initPhase; }

    static void armRegistering(GsmController& gsm) {
        gsm._state = GsmState::REGISTERING;
        gsm._cregStat = 0;
        gsm._lastCommandTime = 0;
        gsm._retryCount = 0;
        gsm._stateStartTime = millis();
        gsm.resetAwait();
    }

    static void armGprs(GsmController& gsm) {
        gsm._state = GsmState::GPRS_SETUP;
        gsm._lastCommandTime = 0;
        gsm._gprsCfgStep = 0;
        gsm._retryCount = 0;
        gsm.resetAwait();
    }

    static void armSilentBaud(GsmController& gsm) {
        gsm._state = GsmState::INIT;
        gsm._waitFirstUntilMs = 0;
        gsm._postLockQuietUntilMs = 0;
        gsm._baudSearchActive = true;
        gsm._baudSearchRound = 1;
        gsm._baudSearchBaudIdx = Gsm::UART_BAUD_CANDIDATE_COUNT - 1;
        gsm._initPhase = GsmInitPhase::BsAwaitAt;
        gsm._verifiedModemContactSinceStop = false;
        gsm.beginAwait(GsmController::AwaitKind::OK, 1);
        test_clock::advance(2);
    }
};

void setUp() {
    test_clock::set(100000);
    test_log::clear();
    Serial.clearTx();
    core.getErrorManager().clear();
}

void tearDown() {}

static void test_creg_home_moves_to_gprs_setup() {
    GsmController gsm;
    GsmFsmTestAccess::armRegistering(gsm);
    GsmFsmTestAccess::reg(gsm);
    GsmFsmTestAccess::flushTx(gsm);
    TEST_ASSERT_NOT_NULL(strstr(Serial.tx(), "AT+CREG?"));

    GsmFsmTestAccess::acceptOk(gsm, "+CREG: 0,1");
    GsmFsmTestAccess::reg(gsm);
    TEST_ASSERT_EQUAL_INT((int)GsmState::GPRS_SETUP, (int)GsmFsmTestAccess::state(gsm));
    TEST_ASSERT_FALSE(core.getErrorManager().isActive(ErrorCode::GSM_REG_FAIL));
}

static void test_gprs_apn_then_warm_bearer_reaches_ready() {
    GsmController gsm;
    GsmFsmTestAccess::armGprs(gsm);

    GsmFsmTestAccess::gprs(gsm);
    GsmFsmTestAccess::flushTx(gsm);
    TEST_ASSERT_NOT_NULL(strstr(Serial.tx(), "AT+SAPBR=3,1,\"Contype\",\"GPRS\""));
    GsmFsmTestAccess::acceptOk(gsm);
    GsmFsmTestAccess::gprs(gsm);
    GsmFsmTestAccess::flushTx(gsm);
    TEST_ASSERT_NOT_NULL(strstr(Serial.tx(), "AT+SAPBR=3,1,\"APN\",\"internet\""));
    GsmFsmTestAccess::acceptOk(gsm);
    GsmFsmTestAccess::gprs(gsm);
    TEST_ASSERT_EQUAL_INT((int)GsmState::GPRS_ATTACH, (int)GsmFsmTestAccess::state(gsm));

    Serial.clearTx();
    GsmFsmTestAccess::attach(gsm);
    GsmFsmTestAccess::flushTx(gsm);
    TEST_ASSERT_NOT_NULL(strstr(Serial.tx(), "AT+SAPBR=2,1"));
    GsmFsmTestAccess::acceptOk(gsm, "+SAPBR: 1,1,\"10.1.2.3\"");
    GsmFsmTestAccess::attach(gsm);
    GsmFsmTestAccess::flushTx(gsm);
    TEST_ASSERT_EQUAL_INT((int)GsmState::READY, (int)GsmFsmTestAccess::state(gsm));
    TEST_ASSERT_NOT_NULL(strstr(Serial.tx(), "AT+CSQ"));
}

static void test_silent_modem_baud_search_stops_at_round_limit() {
    GsmController gsm;
    GsmFsmTestAccess::armSilentBaud(gsm);

    bool stopped = false;
    for (int i = 0; i < 80 && !stopped; i++) {
        GsmFsmTestAccess::init(gsm);
        if (GsmFsmTestAccess::state(gsm) == GsmState::ERROR) {
            stopped = true;
            break;
        }
        GsmFsmTestAccess::flushTx(gsm);
        const GsmInitPhase phase = GsmFsmTestAccess::phase(gsm);
        if (phase == GsmInitPhase::BsCooldown) {
            test_clock::advance(Gsm::BAUD_SEARCH_ROUND_COOLDOWN_MS);
        } else if (phase == GsmInitPhase::BsSettle) {
            test_clock::advance(Gsm::UART_SETTLE_MS + Gsm::BAUD_SEARCH_SETTLE_PER_BAUD_MS);
        } else if (phase == GsmInitPhase::BsAwaitAt) {
            test_clock::advance(Gsm::AT_OK_TIMEOUT_MS + 1);
            GsmFsmTestAccess::dropTimedOutAt(gsm);
        }
    }

    TEST_ASSERT_TRUE(stopped);
    TEST_ASSERT_EQUAL_INT((int)GsmState::ERROR, (int)GsmFsmTestAccess::state(gsm));
    TEST_ASSERT_TRUE(core.getErrorManager().isActive(ErrorCode::GSM_NO_RESPONSE));
    TEST_ASSERT_TRUE(test_log::contains("baud search: round limit exceeded"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_creg_home_moves_to_gprs_setup);
    RUN_TEST(test_gprs_apn_then_warm_bearer_reaches_ready);
    RUN_TEST(test_silent_modem_baud_search_stops_at_round_limit);
    return UNITY_END();
}
