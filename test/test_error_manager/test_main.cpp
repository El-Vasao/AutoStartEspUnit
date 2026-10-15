#include <unity.h>

#include <string.h>

#include "core/ErrorManager.h"

void setUp() { test_clock::set(10000); }
void tearDown() {}

static ErrorManager fresh() {
    ErrorManager em;
    em.clearRtc();
    return em;
}

static void test_set_makes_code_current_and_active() {
    ErrorManager em = fresh();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ErrorCode::NONE, (uint8_t)em.get());
    em.set(ErrorCode::GSM_NO_RESPONSE);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ErrorCode::GSM_NO_RESPONSE, (uint8_t)em.get());
    TEST_ASSERT_TRUE(em.isActive(ErrorCode::GSM_NO_RESPONSE));
    TEST_ASSERT_EQUAL_UINT32(10, em.getTime());
}

static void test_multiple_codes_stay_active_and_clear_independently() {
    ErrorManager em = fresh();
    em.set(ErrorCode::GSM_NO_RESPONSE);
    em.set(ErrorCode::RELAY_TIMEOUT);
    ErrorSnapshotEntry act[ErrorHistory::CAPACITY];
    TEST_ASSERT_EQUAL_UINT8(2, em.copyActive(act, ErrorHistory::CAPACITY));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ErrorCode::RELAY_TIMEOUT, act[0].code);  // newest first

    em.clear(ErrorCode::RELAY_TIMEOUT);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ErrorCode::GSM_NO_RESPONSE, (uint8_t)em.get());
    TEST_ASSERT_EQUAL_UINT8(1, em.copyActive(act, ErrorHistory::CAPACITY));

    em.clear();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ErrorCode::NONE, (uint8_t)em.get());
    TEST_ASSERT_EQUAL_UINT8(0, em.copyActive(act, ErrorHistory::CAPACITY));
    TEST_ASSERT_EQUAL_UINT8(2, em.historyCount());  // history kept
}

static void test_repeated_set_refreshes_and_moves_to_front() {
    ErrorManager em = fresh();
    em.set(ErrorCode::GSM_NO_RESPONSE);
    em.set(ErrorCode::RELAY_TIMEOUT);
    test_clock::set(20000);
    em.set(ErrorCode::GSM_NO_RESPONSE);
    TEST_ASSERT_EQUAL_UINT8(2, em.historyCount());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ErrorCode::GSM_NO_RESPONSE, (uint8_t)em.historyAt(0).code);
    TEST_ASSERT_EQUAL_UINT32(20, em.historyAt(0).uptimeSec);
}

static void test_ring_evicts_oldest() {
    ErrorManager em = fresh();
    const ErrorCode codes[] = {ErrorCode::FS_MOUNT_FAIL, ErrorCode::CONFIG_MISSING, ErrorCode::GSM_REG_FAIL,
                               ErrorCode::GSM_APN_FAIL, ErrorCode::ADC_READ_FAIL, ErrorCode::ONEWIRE_BUS_ERR};
    for (ErrorCode c : codes) em.set(c);
    TEST_ASSERT_EQUAL_UINT8(ErrorHistory::CAPACITY, em.historyCount());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ErrorCode::ONEWIRE_BUS_ERR, (uint8_t)em.historyAt(0).code);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ErrorCode::CONFIG_MISSING,
                            (uint8_t)em.historyAt(ErrorHistory::CAPACITY - 1).code);
}

static void test_undelivered_until_marked_by_fingerprint() {
    ErrorManager em = fresh();
    em.set(ErrorCode::MQTT_CONNECT_FAIL);
    ErrorSnapshotEntry out[ErrorHistory::CAPACITY];
    TEST_ASSERT_EQUAL_UINT8(1, em.copyUndelivered(out, ErrorHistory::CAPACITY));
    TEST_ASSERT_EQUAL_STRING("MQTT connection failed", out[0].msg);
    em.markDelivered(out, 1);
    TEST_ASSERT_EQUAL_UINT8(0, em.copyUndelivered(out, ErrorHistory::CAPACITY));

    // Same code later = new fingerprint (refresh) is still delivered-flagged in place.
    test_clock::set(30000);
    em.set(ErrorCode::START_FAILED);
    TEST_ASSERT_EQUAL_UINT8(1, em.copyUndelivered(out, ErrorHistory::CAPACITY));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ErrorCode::START_FAILED, out[0].code);
}

static void test_rtc_round_trip_restores_actives_and_redelivers() {
    ErrorManager a = fresh();
    a.set(ErrorCode::GSM_NO_RESPONSE);
    a.set(ErrorCode::RELAY_TIMEOUT);
    a.clear(ErrorCode::GSM_NO_RESPONSE);
    ErrorSnapshotEntry out[ErrorHistory::CAPACITY];
    a.markDelivered(out, a.copyUndelivered(out, ErrorHistory::CAPACITY));

    ErrorManager b;
    b.loadFromRtc();
    TEST_ASSERT_EQUAL_UINT8(2, b.historyCount());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ErrorCode::RELAY_TIMEOUT, (uint8_t)b.get());
    TEST_ASSERT_FALSE(b.isActive(ErrorCode::GSM_NO_RESPONSE));
    TEST_ASSERT_EQUAL_UINT8(2, b.copyUndelivered(out, ErrorHistory::CAPACITY));  // reboot → redeliver
    TEST_ASSERT_TRUE(b.historyAt(0).fromRtc);
}

static void test_cleared_rtc_is_not_loaded() {
    ErrorManager a = fresh();
    a.set(ErrorCode::RELAY_TIMEOUT);
    a.clearRtc();
    ErrorManager b;
    b.loadFromRtc();
    TEST_ASSERT_EQUAL_UINT8(0, b.historyCount());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ErrorCode::NONE, (uint8_t)b.get());
}

static void test_boot_reset_is_active_only_without_other_actives() {
    ErrorManager em = fresh();
    em.recordBootReset(ErrorCode::WDT_RESET);
    TEST_ASSERT_TRUE(em.isActive(ErrorCode::WDT_RESET));

    ErrorManager em2 = fresh();
    em2.set(ErrorCode::GSM_NO_RESPONSE);
    em2.recordBootReset(ErrorCode::PANIC_RESET);
    TEST_ASSERT_FALSE(em2.isActive(ErrorCode::PANIC_RESET));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ErrorCode::PANIC_RESET, (uint8_t)em2.historyAt(0).code);
}

static void test_every_error_message_fits_snapshot_buffer() {
    for (int c = 0; c <= 255; c++) {
        const char* msg = errorCodeToString(static_cast<ErrorCode>(c));
        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE_MESSAGE(strlen(msg) < ErrorHistory::MSG_MAX, msg);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_set_makes_code_current_and_active);
    RUN_TEST(test_multiple_codes_stay_active_and_clear_independently);
    RUN_TEST(test_repeated_set_refreshes_and_moves_to_front);
    RUN_TEST(test_ring_evicts_oldest);
    RUN_TEST(test_undelivered_until_marked_by_fingerprint);
    RUN_TEST(test_rtc_round_trip_restores_actives_and_redelivers);
    RUN_TEST(test_cleared_rtc_is_not_loaded);
    RUN_TEST(test_boot_reset_is_active_only_without_other_actives);
    RUN_TEST(test_every_error_message_fits_snapshot_buffer);
    return UNITY_END();
}
