// src/core/TimeSyncManager.cpp
#include "core/TimeSyncManager.h"

#include "config/Config.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "gsm/GsmController.h"
#include "common/ErrorCodes.h"
#include "common/Utils.h"
#include "common/Logger.h"

#include <sys/time.h>
#include <stddef.h>
#include <string.h>

/**
 * @file TimeSyncManager.cpp
 * @brief Мягкие настенные часы и каскад времени SIM800 (CCLK → CIPSHUT → CIPGSMLOC → CNTP).
 *
 * CCLK и CIPGSMLOC всегда идут в полном каскаде. CNTP только при time.enabled (NTP).
 * При поднятом MQTT: сначала пробы CCLK-lite; тяжёлый каскад отдаёт MQTT после промахов lite.
 * Неудачи используют ступенчатый backoff (2→5→15→30 мин).
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

const char* sourceFromGsm(GsmController::TimeSource s) {
    switch (s) {
        case GsmController::TimeSource::Cclk: return "cclk";
        case GsmController::TimeSource::Cipgsmloc: return "cipgsmloc";
        case GsmController::TimeSource::Cntp: return "cntp";
        default: return "modem";
    }
}

bool isTimeSyncError(ErrorCode c) {
    return c == ErrorCode::TIME_CCLK_FAIL || c == ErrorCode::TIME_CIPGSMLOC_FAIL ||
           c == ErrorCode::TIME_CNTP_FAIL;
}

void clearTimeSyncErrorsIfActive_() {
    ErrorManager& em = core.getErrorManager();
    if (isTimeSyncError(em.get())) {
        em.clear();
    }
}

void applyTimeSyncFailMask_(uint8_t mask) {
    if (mask == 0) return;
    ErrorManager& em = core.getErrorManager();
    if (mask & GsmController::TimeFailCclk) {
        em.set(ErrorCode::TIME_CCLK_FAIL);
    }
    if (mask & GsmController::TimeFailCipgsmloc) {
        em.set(ErrorCode::TIME_CIPGSMLOC_FAIL);
    }
    if (mask & GsmController::TimeFailCntp) {
        em.set(ErrorCode::TIME_CNTP_FAIL);
    }
}

const char* ntpServerForCascade_(const TimeConfig& tcfg) {
    if (!tcfg.enabled) return "";
    return tcfg.ntp_server;
}

uint32_t backoffDelayMs_(uint8_t step) {
    switch (step) {
        case 0: return Timing::TIME_SYNC_RETRY_MS;
        case 1: return Timing::TIME_SYNC_BACKOFF_5MIN_MS;
        case 2: return Timing::TIME_SYNC_BACKOFF_15MIN_MS;
        default: return Timing::TIME_SYNC_BACKOFF_30MIN_MS;
    }
}
} // namespace

TimeSyncManager::TimeSyncManager(Config& config, GsmController& gsm)
    : _config(config), _gsm(gsm) {}

void TimeSyncManager::begin() {
    logger.log(LogModule::TimeSyncManager, "start\n");
    loadFromRtc_();
    _forceRequest = true;
    _nextAttemptMs = 0;
    _bootAttemptStarted = false;
    _bootDeadlineMs = 0;
    _yieldPhase = YieldPhase::Idle;
    _yieldDeadlineMs = 0;
    _yieldCascadeStarted = false;
    _bootTimeSettled = false;
    resetFailBackoff_();
    _cclkLiteStarted = false;
    _heavyAttempt = false;
}

void TimeSyncManager::setLastSource_(const char* source) {
    strlcpy(_lastSource, source ? source : "", sizeof(_lastSource));
}

void TimeSyncManager::markBootTimeSettled_(const char* why) {
    if (_bootTimeSettled) return;
    _bootTimeSettled = true;
    logger.log(LogModule::TimeSyncManager, "boot time set (%s)\n", why ? why : "?");
}

void TimeSyncManager::clearYield_(const char* why) {
    if (_yieldPhase == YieldPhase::Idle) return;
    logger.log(LogModule::TimeSyncManager, "yield end (%s)\n", why ? why : "?");
    _yieldPhase = YieldPhase::Idle;
    _yieldDeadlineMs = 0;
    _yieldCascadeStarted = false;
}

void TimeSyncManager::armYieldDeadline_(uint32_t now) {
    if (_yieldDeadlineMs != 0) return;
    _yieldDeadlineMs = now + Gsm::BOOT_TIME_BUDGET_MS;
    logger.log(LogModule::TimeSyncManager, "yield budget %u ms\n", (unsigned)Gsm::BOOT_TIME_BUDGET_MS);
}

void TimeSyncManager::resetFailBackoff_() {
    _failStep = 0;
    _liteMisses = 0;
}

void TimeSyncManager::scheduleRetry_(uint32_t now) {
    const uint32_t delay = backoffDelayMs_(_failStep);
    _nextAttemptMs = now + delay;
    logger.log(LogModule::TimeSyncManager, "retry in %lu ms (pause step %u, liteMisses=%u)\n",
               (unsigned long)delay, (unsigned)_failStep, (unsigned)_liteMisses);
    if (_failStep < Timing::TIME_SYNC_BACKOFF_MAX_STEP) {
        _failStep = static_cast<uint8_t>(_failStep + 1);
    }
}

void TimeSyncManager::scheduleLiteRetry_(uint32_t now) {
    _nextAttemptMs = now + Timing::TIME_SYNC_LITE_BUSY_RETRY_MS;
    logger.log(LogModule::TimeSyncManager, "lite retry in %lu ms (liteMisses=%u)\n",
               (unsigned long)Timing::TIME_SYNC_LITE_BUSY_RETRY_MS, (unsigned)_liteMisses);
}

void TimeSyncManager::notifyMqttDrainedForSync() {
    if (_yieldPhase != YieldPhase::NeedMqttDrain) return;
    _yieldPhase = YieldPhase::CascadeArmed;
    logger.log(LogModule::TimeSyncManager, "MQTT flushed, cascade armed\n");
}

bool TimeSyncManager::wantHeavyCascade_() const {
    return _forceRequest || _liteMisses >= Timing::TIME_SYNC_LITE_BEFORE_HEAVY;
}

bool TimeSyncManager::tryStartCascade_(uint32_t now, const char* ntpServer, int8_t tzOffsetHours) {
    if (!_bootTimeSettled && _bootDeadlineMs == 0) {
        _bootDeadlineMs = now + Gsm::BOOT_TIME_BUDGET_MS;
        logger.log(LogModule::TimeSyncManager, "boot time budget %u ms\n", (unsigned)Gsm::BOOT_TIME_BUDGET_MS);
    }

    // ntp_server может быть пустым: CCLK и CIPGSMLOC всё равно идут; CNTP внутри GSM пропускается.
    if (!_gsm.requestTimeSync(ntpServer, tzOffsetHours)) {
        return false;
    }
    _bootAttemptStarted = true;
    _forceRequest = false;
    _heavyAttempt = true;
    _cclkLiteStarted = false;
    // Нижняя граница до успеха или scheduleRetry_ при неудаче.
    _nextAttemptMs = now + Timing::TIME_SYNC_RETRY_MS;
    return true;
}

bool TimeSyncManager::tryStartCclkLite_(uint32_t now, int8_t tzOffsetHours) {
    if (!_gsm.requestCclkProbe(tzOffsetHours)) {
        return false;
    }
    _forceRequest = false;
    _heavyAttempt = false;
    _cclkLiteStarted = true;
    _nextAttemptMs = now + Timing::TIME_SYNC_RETRY_MS;
    logger.log(LogModule::TimeSyncManager, "CCLK-lite started\n");
    return true;
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
    const int32_t offSec = (int32_t)tzOffsetHours() * (int32_t)Time::SEC_PER_HOUR;
    time_t local = utc + (time_t)offSec;
    if (!gmtime_r(&local, &out)) return false;
    return true;
}

void TimeSyncManager::applyEpochUtc(time_t epochUtc, const char* source) {
    applyEpochInternal_(epochUtc, source ? source : "override", true);
}

void TimeSyncManager::applyEpochInternal_(time_t epochUtc, const char* source, bool persistRtc) {
    if (epochUtc < TimeSync::MIN_SANE_EPOCH_UTC) { // проверка вменяемости: раньше ~2023-11
        logger.log(LogModule::TimeSyncManager, "rejected epoch=%ld source=%s\n", (long)epochUtc, source ? source : "?");
        return;
    }
    struct timeval tv;
    tv.tv_sec = epochUtc;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    _synced = true;
    _lastSyncMs = millis();
    setLastSource_(source);
    if (persistRtc) saveToRtc_();
    clearTimeSyncErrorsIfActive_();
    resetFailBackoff_();
    _cclkLiteStarted = false;
    _heavyAttempt = false;
    logger.log(LogModule::TimeSyncManager, "synced epoch=%ld tz=%+dh source=%s\n", (long)epochUtc, (int)tzOffsetHours(),
               source ? source : "?");
    markBootTimeSettled_("ok");
    clearYield_("ok");
}

void TimeSyncManager::loadFromRtc_() {
    RtcTimePod rec = s_rtcTime;
    if (rec.magic != RTC_MAGIC) return;
    uint16_t crc = calculateCRC16((const uint8_t*)&rec, offsetof(RtcTimePod, crc));
    if (crc != rec.crc) return;
    if (rec.epoch < TimeSync::MIN_SANE_EPOCH_UTC) return;

    struct timeval tv;
    tv.tv_sec = (time_t)rec.epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    _synced = true;
    _lastSyncMs = millis();
    setLastSource_("rtc");
    logger.log(LogModule::TimeSyncManager, "restored from RTC epoch=%ld\n", (long)rec.epoch);
}

void TimeSyncManager::saveToRtc_() {
    RtcTimePod rec;
    rec.magic = RTC_MAGIC;
    rec.epoch = (int32_t)epochUtc();
    rec.tz_offset_hours = static_cast<int8_t>(tzOffsetHours());
    rec._pad[0] = 0;
    rec.crc = calculateCRC16((const uint8_t*)&rec, offsetof(RtcTimePod, crc));
    s_rtcTime = rec;
}

void TimeSyncManager::update() {
    const auto& tcfg = _config.getBase().time;
    const uint32_t now = millis();
    const char* ntpSrv = ntpServerForCascade_(tcfg);

    if (!_bootTimeSettled) {
        if (_bootDeadlineMs != 0 && (int32_t)(now - _bootDeadlineMs) >= 0) {
            markBootTimeSettled_("budget");
        }
    }

    // Всегда забирать успех модема, чтобы смена на полпути не застряла в автомате времени GSM.
    time_t got = 0;
    GsmController::TimeSource src = GsmController::TimeSource::None;
    int8_t tzFromModem = 0;
    bool tzValid = false;
    if (_gsm.takeTimeEpochUtc(got, &src, &tzFromModem, &tzValid)) {
        if (tzValid) {
            if (!_config.setTzOffsetHours(tzFromModem)) {
                logger.log(LogModule::TimeSyncManager, "setTzOffsetHours(%+d) failed; previous TZ kept\n",
                           (int)tzFromModem);
            }
        }
        applyEpochInternal_(got, sourceFromGsm(src), true);
        _forceRequest = false;
        const uint32_t intervalMs =
            tcfg.sync_interval_hours ? (tcfg.sync_interval_hours * Time::MS_PER_HOUR)
                                     : (Defaults::TIME_SYNC_INTERVAL_HOURS * Time::MS_PER_HOUR);
        _nextAttemptMs = now + intervalMs;
        return;
    }

    // Окончательный провал каскада → код(ы) ErrorCode по методам.
    uint8_t failMask = 0;
    if (_gsm.takeTimeSyncFail(failMask)) {
        applyTimeSyncFailMask_(failMask);
        markBootTimeSettled_("fail");
        clearYield_("fail");
        _forceRequest = false;
        _cclkLiteStarted = false;
        if (_heavyAttempt) {
            _liteMisses = 0; // после тяжёлого снова пробовать lite до следующего слива
            _heavyAttempt = false;
            scheduleRetry_(now); // fail-backoff только после настоящей тяжёлой попытки
        } else {
            if (_liteMisses < 255) _liteMisses++;
            _heavyAttempt = false;
            scheduleLiteRetry_(now);
        }
        return;
    }

    // Мягкий промах CCLK-lite (устарело / tcp_epoch / повторное присоединение) — без fail-backoff.
    if (_cclkLiteStarted && !_gsm.timeSyncBusy()) {
        _cclkLiteStarted = false;
        if (_liteMisses < 255) _liteMisses++;
        logger.log(LogModule::TimeSyncManager, "CCLK-lite soft miss (liteMisses=%u)\n", (unsigned)_liteMisses);
        scheduleLiteRetry_(now);
        return;
    }

    // Каскад на старте закончился без успеха (например, запрос так и не начался).
    if (!_bootTimeSettled && _bootAttemptStarted && !_gsm.timeSyncBusy()) {
        markBootTimeSettled_("attempt_done");
    }

    // Бюджет yield: отпустить фазу удержания MQTT; `_heavyAttempt` держать, пока каскад GSM ещё идёт.
    if (_yieldPhase != YieldPhase::Idle && _yieldDeadlineMs != 0 &&
        (int32_t)(now - _yieldDeadlineMs) >= 0) {
        clearYield_("budget");
        _forceRequest = false;
        if (!_gsm.timeSyncBusy()) {
            _heavyAttempt = false;
            scheduleRetry_(now);
        } else {
            // Каскад ещё идёт — takeFail/ok сами назначат время; держать короткий нижний порог.
            _nextAttemptMs = now + Timing::TIME_SYNC_RETRY_MS;
        }
    }

    // Каскад yield закончился без результата (неудача / отмена) — отпустить MQTT.
    // (Обычный путь неудачи уже делает clearYield и scheduleRetry через takeTimeSyncFail.)
    if (_yieldPhase == YieldPhase::CascadeArmed && _yieldCascadeStarted && !_gsm.timeSyncBusy()) {
        clearYield_("attempt_done");
        _forceRequest = false;
        _heavyAttempt = false;
        scheduleRetry_(now);
    }

    if (_gsm.timeSyncBusy()) return;

    // --- Путь yield: ждём слив CellularCore, затем старт каскада ---
    if (_yieldPhase == YieldPhase::NeedMqttDrain) {
        return;
    }

    if (_yieldPhase == YieldPhase::CascadeArmed) {
        if (_gsm.tcpSocketActive() || _gsm.tcpBusBusy()) return;
        if (_gsm.modemServiceEpochBusy()) return;
        if (tryStartCascade_(now, ntpSrv, tcfg.tz_offset_hours)) {
            _yieldCascadeStarted = true;
        } else {
            scheduleLiteRetry_(now); // занято — не усиливать fail-backoff
        }
        // Оставаться взведённым, пока занятость не кончится (успех → apply снимает; неудача → takeTimeSyncFail) или не истечёт бюджет.
        return;
    }

    // --- Простой: проверка срока ---
    if (!_forceRequest && _nextAttemptMs != 0 && (int32_t)(now - _nextAttemptMs) < 0) return;

    if (!_gsm.isReady()) return;

    // Не спорить с исключительной эпохой голоса/SMS.
    if (_gsm.modemServiceEpochBusy() || _gsm.serviceEpochNeedsMqttDrain()) return;

    // MQTT CIP поднят: CCLK-lite без слива либо тяжёлый yield после промахов или force.
    if (_gsm.tcpSocketActive() || _gsm.tcpBusBusy()) {
        if (wantHeavyCascade_()) {
            _yieldPhase = YieldPhase::NeedMqttDrain;
            armYieldDeadline_(now);
            logger.log(LogModule::TimeSyncManager, "yield: MQTT flush needed for cascade (liteMisses=%u force=%u)\n",
                       (unsigned)_liteMisses, (unsigned)_forceRequest);
            return;
        }
        if (!tryStartCclkLite_(now, tcfg.tz_offset_hours)) {
            scheduleLiteRetry_(now);
        }
        return;
    }

    // TCP уже опущен — сразу полный каскад (старт или после отключения).
    if (!tryStartCascade_(now, ntpSrv, tcfg.tz_offset_hours)) {
        scheduleLiteRetry_(now);
    }
}
