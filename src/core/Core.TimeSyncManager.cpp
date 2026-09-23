// src/core/Core.TimeSyncManager.cpp
#include "core/TimeSyncManager.h"

#include "config/Config.h"
#include "gsm/GSMController.h"
#include "common/Utils.h"
#include "common/Logger.h"

#include <sys/time.h>
#include <string.h>

/**
 * @file Core.TimeSyncManager.cpp
 * @brief Soft wall clock + SIM800 CNTP orchestration (TCP-serialized with MQTT).
 */

namespace {
struct RtcTimePod {
    uint32_t magic;
    int32_t epoch;
    int8_t tz_offset_hours;
    uint8_t _pad[1];
    uint16_t crc;
} __attribute__((aligned(4)));

RTC_DATA_ATTR RtcTimePod s_rtcTime;
} // namespace

TimeSyncManager::TimeSyncManager(Config& config, GSMController& gsm)
    : _config(config), _gsm(gsm) {}

void TimeSyncManager::begin() {
    logger.log("[TimeSync] begin\n");
    loadFromRtc_();
    _forceRequest = true;
    _nextAttemptMs = 0;
    _bootNtpAttemptStarted = false;
    _bootNtpDeadlineMs = 0;

    const auto& tcfg = _config.getBase().time;
    if (!tcfg.enabled || !tcfg.ntp_server[0]) {
        markBootNtpSettled_("disabled_or_empty");
    } else {
        _bootNtpSettled = false;
    }
}

void TimeSyncManager::markBootNtpSettled_(const char* why) {
    if (_bootNtpSettled) return;
    _bootNtpSettled = true;
    logger.log("[TimeSync] boot NTP settled (%s)\n", why ? why : "?");
}

bool TimeSyncManager::isStale() const {
    if (!_synced) return true;
    if (_lastSyncMs == 0) return true;
    return (millis() - _lastSyncMs) >= Timing::TIME_SYNC_STALE_MS;
}

time_t TimeSyncManager::epochUtc() const {
    time_t now = 0;
    time(&now);
    return now;
}

int16_t TimeSyncManager::tzOffsetHours() const {
    return _config.getBase().time.tz_offset_hours;
}

bool TimeSyncManager::localBrokenDown(struct tm& out) const {
    if (!_synced) return false;
    time_t utc = epochUtc();
    const int32_t offSec = (int32_t)tzOffsetHours() * 3600;
    time_t local = utc + (time_t)offSec;
    if (!gmtime_r(&local, &out)) return false;
    return true;
}

void TimeSyncManager::requestSync() {
    _forceRequest = true;
    _nextAttemptMs = 0;
}

void TimeSyncManager::applyEpochUtc(time_t epochUtc, const char* source) {
    applyEpochInternal_(epochUtc, source ? source : "override", true);
}

void TimeSyncManager::applyEpochInternal_(time_t epochUtc, const char* source, bool persistRtc) {
    if (epochUtc < 1700000000L) { // sanity: before ~2023-11
        logger.log("[TimeSync] reject epoch=%ld source=%s\n", (long)epochUtc, source ? source : "?");
        return;
    }
    struct timeval tv;
    tv.tv_sec = epochUtc;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    _synced = true;
    _lastSyncMs = millis();
    if (persistRtc) saveToRtc_();
    logger.log("[TimeSync] synced epoch=%ld tz=%+dh source=%s\n", (long)epochUtc, (int)tzOffsetHours(),
               source ? source : "?");
    markBootNtpSettled_("ok");
}

void TimeSyncManager::loadFromRtc_() {
    RtcTimePod rec = s_rtcTime;
    if (rec.magic != RTC_MAGIC) return;
    uint16_t crc = calculateCRC16((const uint8_t*)&rec, sizeof(rec) - sizeof(rec.crc));
    if (crc != rec.crc) return;
    if (rec.epoch < 1700000000L) return;

    struct timeval tv;
    tv.tv_sec = (time_t)rec.epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    _synced = true;
    _lastSyncMs = millis();
    logger.log("[TimeSync] restored from RTC epoch=%ld\n", (long)rec.epoch);
}

void TimeSyncManager::saveToRtc_() {
    RtcTimePod rec;
    rec.magic = RTC_MAGIC;
    rec.epoch = (int32_t)epochUtc();
    rec.tz_offset_hours = static_cast<int8_t>(tzOffsetHours());
    rec._pad[0] = 0;
    rec.crc = calculateCRC16((const uint8_t*)&rec, sizeof(rec) - sizeof(rec.crc));
    s_rtcTime = rec;
}

void TimeSyncManager::update() {
    const auto& tcfg = _config.getBase().time;
    const uint32_t now = millis();

    if (!_bootNtpSettled) {
        if (!tcfg.enabled || !tcfg.ntp_server[0]) {
            markBootNtpSettled_("disabled_or_empty");
        } else if (_bootNtpDeadlineMs != 0 && (int32_t)(now - _bootNtpDeadlineMs) >= 0) {
            markBootNtpSettled_("budget");
        }
    }

    // Always drain modem result so a mid-flight disable cannot stick the GSM NTP FSM.
    time_t got = 0;
    if (_gsm.takeNtpEpochUtc(got)) {
        if (tcfg.enabled && tcfg.ntp_server[0]) {
            applyEpochInternal_(got, "cntp", true);
            _forceRequest = false;
            const uint32_t intervalMs =
                tcfg.sync_interval_sec ? (tcfg.sync_interval_sec * 1000UL) : 21600000UL;
            _nextAttemptMs = now + intervalMs;
        } else {
            logger.log("[TimeSync] discard CNTP result (disabled or empty server)\n");
            markBootNtpSettled_("discard");
        }
        return;
    }

    // Boot attempt finished without success (URC fail / tcp_taken / etc.).
    if (!_bootNtpSettled && _bootNtpAttemptStarted && !_gsm.ntpSyncBusy()) {
        markBootNtpSettled_("attempt_done");
    }

    if (!tcfg.enabled || !tcfg.ntp_server[0]) return;

    if (_gsm.ntpSyncBusy()) return;

    if (!_forceRequest && _nextAttemptMs != 0 && (int32_t)(now - _nextAttemptMs) < 0) return;

    if (!_gsm.isReady()) return;
    // Hard serialize with MQTT CIP — no NTP while TCP is up/connecting.
    if (_gsm.tcpSocketActive() || _gsm.tcpBusBusy()) return;

    if (!_bootNtpSettled && _bootNtpDeadlineMs == 0) {
        _bootNtpDeadlineMs = now + GSM::BOOT_NTP_BUDGET_MS;
        logger.log("[TimeSync] boot NTP budget %u ms\n", (unsigned)GSM::BOOT_NTP_BUDGET_MS);
    }

    if (!_gsm.requestNtpSync(tcfg.ntp_server, tcfg.tz_offset_hours)) {
        _nextAttemptMs = now + Timing::TIME_SYNC_RETRY_MS;
        return;
    }
    _bootNtpAttemptStarted = true;
    _forceRequest = false;
    _nextAttemptMs = now + Timing::TIME_SYNC_RETRY_MS;
}
