// include/core/TimeSyncManager.h
#pragma once

#include <Arduino.h>
#include <time.h>

#include "common/Constants.h"

class Config;
class GSMController;

/**
 * Soft wall clock: sync via SIM800 AT+CNTP / CCLK, then settimeofday.
 * Survives soft reboot via RTC_DATA_ATTR snapshot until next network sync.
 *
 * CNTP only when TCP socket is inactive (serialized with MQTT CIP).
 */
class TimeSyncManager {
public:
    TimeSyncManager(Config& config, GSMController& gsm);

    void begin();
    void update();

    bool isSynced() const { return _synced; }
    bool isStale() const;
    time_t epochUtc() const;
    int16_t tzOffsetHours() const;
    /// Local civil time (applies config tz_offset_hours). Returns false if not synced.
    bool localBrokenDown(struct tm& out) const;

    /// Apply epoch from MQTT/admin override (UTC seconds since 1970).
    void applyEpochUtc(time_t epochUtc, const char* source = "override");

    void requestSync();

    /// True when CellularCore may start MQTT (boot NTP done / skipped / timed out).
    bool isBootNtpSettled() const { return _bootNtpSettled; }

private:
    Config& _config;
    GSMController& _gsm;

    bool _synced{false};
    uint32_t _lastSyncMs{0};
    uint32_t _nextAttemptMs{0};
    bool _forceRequest{false};

    bool _bootNtpSettled{false};
    bool _bootNtpAttemptStarted{false};
    uint32_t _bootNtpDeadlineMs{0};

    static constexpr uint32_t RTC_MAGIC = 0xC10C710Eu;

    void loadFromRtc_();
    void saveToRtc_();
    void applyEpochInternal_(time_t epochUtc, const char* source, bool persistRtc);
    void markBootNtpSettled_(const char* why);
};
