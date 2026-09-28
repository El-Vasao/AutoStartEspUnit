// include/core/TimeSyncManager.h
#pragma once

#include <Arduino.h>
#include <time.h>

#include "common/Constants.h"

class Config;
class GSMController;

/**
 * Soft wall clock: cascade CCLK → CIPGSMLOC → CNTP on SIM800, then settimeofday.
 * Survives soft reboot via RTC_DATA_ATTR snapshot until next network sync.
 *
 * Modem cascade needs CIP idle (serialized with MQTT). Periodic sync_interval may
 * request an MQTT drain yield so the full cascade can run while always-on MQTT.
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
    /// Last successful source: "cclk" / "cipgsmloc" / "cntp" / "mqtt" / "rtc" / "override" / "".
    const char* lastSource() const { return _lastSource; }
    /// Local civil time (applies config tz_offset_hours). Returns false if not synced.
    bool localBrokenDown(struct tm& out) const;

    /// Apply epoch from MQTT/admin override (UTC seconds since 1970).
    void applyEpochUtc(time_t epochUtc, const char* source = "override");

    void requestSync();

    /// True when CellularCore may start MQTT (boot time cascade done / skipped / timed out).
    bool isBootTimeSettled() const { return _bootTimeSettled; }
    /// Legacy alias.
    bool isBootNtpSettled() const { return isBootTimeSettled(); }

    /// CellularCore should drain MQTT/CIP before cascade (periodic yield).
    bool needsMqttDrainForSync() const { return _yieldPhase == YieldPhase::NeedMqttDrain; }
    /// Hold MQTT reconnect for drain + cascade (same role as modemServiceEpochBusy for voice).
    bool isSyncYieldEpochBusy() const { return _yieldPhase != YieldPhase::Idle; }
    /// CellularCore finished drainMqttDisconnect_ (or reconnect already off).
    void notifyMqttDrainedForSync();

private:
    enum class YieldPhase : uint8_t {
        Idle = 0,
        NeedMqttDrain,
        CascadeArmed,
    };

    Config& _config;
    GSMController& _gsm;

    bool _synced{false};
    uint32_t _lastSyncMs{0};
    uint32_t _nextAttemptMs{0};
    bool _forceRequest{false};
    char _lastSource[12]{};

    bool _bootTimeSettled{false};
    bool _bootAttemptStarted{false};
    uint32_t _bootDeadlineMs{0};

    YieldPhase _yieldPhase{YieldPhase::Idle};
    uint32_t _yieldDeadlineMs{0};
    bool _yieldCascadeStarted{false};

    static constexpr uint32_t RTC_MAGIC = 0xC10C710Eu;

    void loadFromRtc_();
    void saveToRtc_();
    void applyEpochInternal_(time_t epochUtc, const char* source, bool persistRtc);
    void markBootTimeSettled_(const char* why);
    void setLastSource_(const char* source);
    void clearYield_(const char* why);
    void armYieldDeadline_(uint32_t now);
    bool tryStartCascade_(uint32_t now, const char* ntpServer, int8_t tzOffsetHours);
};
