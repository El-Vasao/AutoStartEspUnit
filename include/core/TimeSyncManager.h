// include/core/TimeSyncManager.h
#pragma once

#include <Arduino.h>
#include <time.h>

#include "common/Constants.h"

class Config;
class GsmController;

/**
 * Мягкие настенные часы: каскад CCLK → CIPSHUT → CIPGSMLOC → (необязательный CNTP) на SIM800, затем settimeofday.
 * CCLK/CIPGSMLOC всегда входят в полный каскад; CNTP только при config time.enabled (NTP).
 * Переживает мягкую перезагрузку снимком RTC_DATA_ATTR до следующей сетевой синхронизации.
 *
 * Пока MQTT CIP поднят: предпочтительны пробы CCLK-lite только по AT (без слива). Полный каскад
 * (CIPGSMLOC/CNTP) просит yield со сливом MQTT после нескольких промахов lite или по флагу force.
 * Неудачные попытки используют ступенчатый backoff (2→5→15→30 мин) вместо фиксированного цикла 120 с.
 */
class TimeSyncManager {
public:
    TimeSyncManager(Config& config, GsmController& gsm);

    void begin();
    void update();

    bool isSynced() const { return _synced; }
    bool isStale() const;
    time_t epochUtc() const;
    int16_t tzOffsetHours() const;
    /// Источник последнего успеха: "cclk" / "cipgsmloc" / "cntp" / "mqtt" / "rtc" / "override" / "".
    const char* lastSource() const { return _lastSource; }
    /// Местное гражданское время (применяется tz_offset_hours из конфига). false, если нет синхронизации.
    bool localBrokenDown(struct tm& out) const;

    /// Применить эпоху из переопределения MQTT/админа (секунды UTC с 1970).
    void applyEpochUtc(time_t epochUtc, const char* source = "override");

    /// true, когда CellularCore может стартовать MQTT (каскад времени на старте завершён, пропущен или истёк).
    bool isBootTimeSettled() const { return _bootTimeSettled; }

    /// CellularCore должен слить MQTT/CIP перед каскадом (периодический yield).
    bool needsMqttDrainForSync() const { return _yieldPhase == YieldPhase::NeedMqttDrain; }
    /// Держать переподключение MQTT на время слива и каскада (та же роль, что modemServiceEpochBusy для голоса).
    /// Остаётся true, пока тяжёлый каскад GSM ещё идёт после бюджета yield (IP владеет модемом).
    bool isSyncYieldEpochBusy() const {
        return _yieldPhase != YieldPhase::Idle || _heavyAttempt;
    }
    /// CellularCore закончил drainMqttDisconnect_ (или переподключение уже выключено).
    void notifyMqttDrainedForSync();

private:
    enum class YieldPhase : uint8_t {
        Idle = 0,
        NeedMqttDrain,
        CascadeArmed,
    };

    Config& _config;
    GsmController& _gsm;

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

    /// Ступенчатый backoff неудачи (0..TIME_SYNC_BACKOFF_MAX_STEP).
    uint8_t _failStep{0};
    /// Мягкие промахи и промахи AT у CCLK-lite с последнего тяжёлого каскада.
    uint8_t _liteMisses{0};
    bool _cclkLiteStarted{false};
    bool _heavyAttempt{false};

    static constexpr uint32_t RTC_MAGIC = 0xC10C710Eu;

    void loadFromRtc_();
    void saveToRtc_();
    void applyEpochInternal_(time_t epochUtc, const char* source, bool persistRtc);
    void markBootTimeSettled_(const char* why);
    void setLastSource_(const char* source);
    void clearYield_(const char* why);
    void armYieldDeadline_(uint32_t now);
    void scheduleRetry_(uint32_t now);
    /// Короткий повтор без продвижения `_failStep` (мягкий промах lite / краткая занятость).
    void scheduleLiteRetry_(uint32_t now);
    void resetFailBackoff_();
    bool tryStartCascade_(uint32_t now, const char* ntpServer, int8_t tzOffsetHours);
    bool tryStartCclkLite_(uint32_t now, int8_t tzOffsetHours);
    bool wantHeavyCascade_() const;
};
