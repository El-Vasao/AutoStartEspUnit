// include/gsm/GSMController.h
#pragma once

#include <Arduino.h>
#include <time.h>
#include "common/Constants.h"
#include "common/ErrorCodes.h"
#include "gsm/GsmModemStack.h"
#include "gsm/GsmInitPhase.h"
#include "gsm/GsmInitFsm.h"

// Состояния GSM-модема
enum class GSMState : uint8_t {
    IDLE,                ///< ожидание
    INIT,                ///< инициализация (AT-команды)
    REGISTERING,         ///< регистрация в сети (AT+CREG?)
    GPRS_SETUP,          ///< настройка SAPBR Contype/APN/USER/PWD
    GPRS_ATTACH,         ///< status-first SAPBR open (probe 2,1 → open 1,1)
    GPRS_GETIP,          ///< подтверждение IP (AT+SAPBR=2,1)
    READY,               ///< модем готов, bearer up
    ERROR                ///< ошибка, требуется перезапуск
};

/**
 * @brief Контроллер GSM-модема SIM800.
 * Реализует асинхронный конечный автомат для регистрации и подключения GPRS.
 * Используется как provider транспортной готовности для MQTT.
 *
 * Важно по железу:
 * - по умолчанию UART0 (`Serial`) используется для AT-трафика с модемом (не для `logger`).
 *   Логи идут в SSE (UI) и не должны вмешиваться в UART0.
 *
 * Важно по памяти:
 * - RX ответа модема — кольцевой буфер `char[_responseBuffer]` фиксированного размера (без Arduino `String`).
 */
class GSMController {
public:
    GSMController();

    // Запуск/перезапуск автомата (после загрузки конфига)
    void begin();

    // Остановить автомат и сбросить runtime-состояние
    void stop();

    // Обновление состояния (вызывать в loop)
    void update();
    void requestReattach() { _reattachRequested = true; }
    void requestModemReboot() { _userRebootRequested = true; }
    /// Clear reattach backoff after a stable MQTT data-plane session (not on every READY entry).
    void clearReattachBackoff();

    // Проверить, готов ли модем к работе (GPRS подключён)
    bool isReady() const { return _state == GSMState::READY; }

    // Получить название оператора
    const char* getOperator() const { return _operator; }

    // Получить уровень сигнала (0-31, 99 = unknown)
    int16_t getSignalQuality() const { return _signal; }

    // BER из последнего AT+CSQ (0..7), или -1 если ещё не было ответа
    int16_t getSignalBer() const { return _signalBer; }

    // Текущая скорость UART, на которой работает модем (по мнению контроллера).
    uint32_t getBaud() const { return _baud; }

    // Сколько ретраев было в текущем состоянии.
    uint8_t getRetryCount() const { return _retryCount; }

    // Возраст текущего состояния (мс).
    uint32_t getStateAgeMs() const { return millis() - _stateStartTime; }

    // Текущее состояние в виде строки
    const char* getStateString() const;

    // Текущее состояние (enum)
    GSMState getState() const { return _state; }

    // Получить ссылку на клиент для MQTT
    Client& getClient() { return _stack.client; }

    /// True while CIP TCP/send/close/recover epoch is active (not plain CSQ/COPS AT).
    bool tcpEpochBusy() const { return _stack.tcp.isTcpEpochBusy(); }
    /// Epoch or any AtSession busy — use for reattach/drain.
    bool tcpBusBusy() const { return _stack.tcp.isBusBusy(); }

    /// True while CIP TCP socket is up or connecting (NTP must not run).
    bool tcpSocketActive() const {
        return _stack.tcp.isConnected() || _stack.tcp.isConnecting();
    }

    /// Sticky TCP RX ring overflow (MQTT must reconnect).
    bool takeTcpRxOverflow() { return _stack.tcp.takeRxOverflow(); }

    /// Log Sim800 +IPD / send-epoch snapshot (MQTT rx_incomplete forensics).
    void logTcpRxForensic(const char* why) { _stack.tcp.logRxForensic(why); }

    /// Legacy: always false — MQTT drains RX ring even mid-send.
    bool shouldDeferMqttRead() const { return _stack.tcp.shouldDeferMqttRead(); }

    /// Who produced the last successful modem time sync (for status/debug).
    enum class TimeSource : uint8_t { None = 0, Cclk, Cipgsmloc, Cntp };

    /// Wall-clock cascade steps (public so busy check can inline).
    enum class TimeStep : uint8_t {
        Idle = 0,
        CclkProbe,
        Cipgsmloc,
        Cntpcid,
        CntpSet,
        CntpRun,
        WaitCntpUrc,
        CclkAfterCntp,
    };

    /// Non-blocking cascade: CCLK/NITZ → CIPGSMLOC → CNTP. Returns false if busy / not ready / TCP active.
    /// `ntpServer` may be empty to skip CNTP after CCLK/CIPGSMLOC fail.
    bool requestTimeSync(const char* ntpServer, int8_t tzOffsetHours);
    /// Alias → requestTimeSync (legacy name).
    bool requestNtpSync(const char* ntpServer, int8_t tzOffsetHours) {
        return requestTimeSync(ntpServer, tzOffsetHours);
    }
    bool timeSyncBusy() const { return _timeStep != TimeStep::Idle; }
    bool ntpSyncBusy() const { return timeSyncBusy(); }
    /// Consumes a successful sync result (UTC epoch). Returns false if none pending.
    bool takeTimeEpochUtc(time_t& epochUtcOut, TimeSource* sourceOut = nullptr);
    bool takeNtpEpochUtc(time_t& epochUtcOut) { return takeTimeEpochUtc(epochUtcOut, nullptr); }

private:
    static constexpr size_t RESPONSE_BUF_SIZE = GSM::RESPONSE_BUFFER_SIZE;
    static constexpr size_t AT_CMD_BUF_SIZE = GSM::CMD_BUFFER_SIZE;

    HardwareSerial* _serial;        ///< указатель на последовательный порт (Serial)
    GsmModemStack _stack;

    GSMState _state;                 ///< текущее состояние
    uint32_t _baud;                  ///< найденная/используемая скорость UART
    uint32_t _stateStartTime;        ///< время входа в текущее состояние (мс)
    uint32_t _lastCommandTime;       ///< время последней отправленной команды (мс)
    uint32_t _lastRxByteMs{0};       ///< время последнего принятого байта (мс), для диагностического flush
    // RX буфер ответа модема (кольцевой буфер). Нужен для простых contains-проверок (OK/ERROR/+CREG...).
    // Важно: без memmove/heap, чтобы выдерживать длинные сессии и шумный UART.
    char _responseBuffer[RESPONSE_BUF_SIZE];
    uint16_t _respHead{0};           ///< индекс первого (самого старого) байта
    uint16_t _respCount{0};          ///< сколько байт валидно в буфере (0..RESPONSE_BUF_SIZE)
    // RX line framing is handled by ModemUart.
    char _operator[TextBytes::Gsm::OPERATOR]; ///< название оператора
    int16_t _signal;                 ///< уровень сигнала
    int16_t _signalBer{-1};          ///< BER из AT+CSQ (0..7) или -1 unknown
    uint8_t _retryCount;             ///< счётчик повторных попыток
    bool _reattachRequested{false};
    bool _userRebootRequested{false};
    uint8_t _rebootStep{0};
    uint32_t _reattachCooldownUntilMs{0};
    uint8_t _reattachBackoffStep{0};

    // --- URC/парсинг строк ---
    int8_t _cregStat{-1};            ///< -1 unknown, иначе stat из +CREG
    int8_t _cgattStat{-1};           ///< -1 unknown, иначе 0/1 из +CGATT
    bool _pdpDeactSeen{false};
    bool _closedSeen{false};
    bool _modemRestartedSeen{false};
    uint32_t _tcpClosedFirstMs{0};
    uint8_t _tcpClosedStreak{0};

    // --- Ожидание ответа на текущую команду (waiter) ---
    enum class AwaitKind : uint8_t { NONE, OK, CREG, IP };
    AwaitKind _await{AwaitKind::NONE};
    uint32_t _awaitDeadlineMs{0};
    bool _awaitOk{false};
    bool _awaitError{false};
    bool _awaitGotCreg{false};
    bool _awaitGotIp{false};
    uint16_t _cmdId{0};
    uint16_t _awaitCmdId{0};
    uint8_t _awaitLinesLeft{0};
    uint8_t _initCfgStep{0};
    uint8_t _gprsCfgStep{0};
    /// GPRS_ATTACH: 0=probe SAPBR=2,1, 1=open SAPBR=1,1, 2=re-probe after open fail.
    uint8_t _attachProbeStep{0};
    char _atCmdBuf[AT_CMD_BUF_SIZE]{}; ///< storage for currently queued AT command (AtSession uses non-owning pointers)

    // Bring-up health
    uint32_t _bringupStartMs{0};
    uint8_t _recoveryLevel{0};
    uint8_t _backoffStep{0};
    uint32_t _cooldownUntilMs{0};
    bool _restartPending{false};
    /// ERROR L2/L3: waiting for CIPSHUT / SAPBR0 before arming begin().
    bool _errorRecoveryCmdPending{false};
    /// Completed L1→L4 cycles since last READY; capped by ERROR_RECOVERY_MAX_CYCLES.
    uint8_t _errorRecoveryCycles{0};
    uint32_t _postLockQuietUntilMs{0};
    uint32_t _waitFirstUntilMs{0};
    uint8_t _waitFirstStep{0};

    /// Липкий до `stop()`: хоть раз успешно пройден шаг `AT`+`AT+CGMI` (SIMCOM) в этой GSM‑сессии.
    bool _verifiedModemContactSinceStop{false};
    uint32_t _firstAtFallbackStartMs{0};
    /// Не раньше этого millis() слать следующий hypothesis AT (spaced backoff).
    uint32_t _hypNextAttemptMs{0};
    /// Подрежим INIT (hypothesis AT/CGMI, baud search, IPR NV, resume CREG…; см. GsmInitFsm.cpp).
    GsmInitPhase _initPhase{GsmInitPhase::None};
    bool _baudSearchActive{false};
    uint8_t _baudSearchRound{0};
    uint8_t _baudSearchBaudIdx{0};
    uint8_t _baudSearchSub{0};
    uint32_t _baudSearchDeadlineMs{0};
    uint32_t _baudCooldownUntilMs{0};
    bool _didIprNvProbeThisCycle{false};
    /// Один best-effort `AT+CFUN=1,1` на цикл до baud search (или уже сделан UI/ERROR L4).
    bool _didPreBaudSearchCfun{false};
    bool _resumeSapbrHadIp{false};
    GSMState _postResumeTarget{GSMState::REGISTERING};
    /// Счётчик повторов `AT+CGATT?` в INIT (SIM busy до CPIN READY).
    uint8_t _initCgattBusyRetries{0};

    /// Last APN credentials written via SAPBR=3 (empty ⇒ never applied this session).
    bool _apnAppliedValid{false};
    char _appliedApn[TextBytes::Gsm::APN]{};
    char _appliedApnUser[TextBytes::Gsm::APN_USER]{};
    char _appliedApnPass[TextBytes::Gsm::APN_PASS]{};

    struct GsmEvent {
        uint32_t ms;
        uint8_t type;
        uint8_t state;
        uint16_t aux;
    };
    static constexpr uint8_t EVENT_RING_SIZE = 8;
    GsmEvent _ev[EVENT_RING_SIZE]{};
    uint8_t _evHead{0};
    void ev(uint8_t type, uint16_t aux = 0);

    uint32_t _lastDiagMs{0};
    uint32_t _lastOperatorMs{0};

    /// Wall-clock cascade (CCLK → CIPGSMLOC → CNTP) — advanced from handleReady when AT bus idle.
    TimeStep _timeStep{TimeStep::Idle};
    char _ntpServer[TextBytes::TimeCfg::NTP_SERVER]{};
    int8_t _ntpTzQuarters{0};
    bool _ntpUrcOk{false};
    bool _ntpUrcFail{false};
    bool _timeResultReady{false};
    bool _timeCmdSent{false};
    time_t _timeEpochUtc{0};
    TimeSource _timeSource{TimeSource::None};
    uint32_t _timeDeadlineMs{0};
    char _cclkSnap[48]{};
    char _cipgsmlocSnap[64]{};

    /// Редкая диагностика «застряли»: снимок state/init/await не меняется дольше интервала.
    uint32_t _gsmStallPrevFp{0};
    uint32_t _gsmStallFpSinceMs{0};
    uint32_t _gsmStallLastLogMs{0};

    void flushInput();
    void sendRawLogged(const char* s, const char* tag = "RAW");
    void logRxSnippet(const char* tag) const;
    bool gsmCgmiResponseManufacturerOk() const;
    bool gsmParseStoredIprBaud(uint32_t& baudOut) const;
    void gsmResetSessionAfterStop();
    /// После soft-reboot модема: снять sticky verify, чтобы PreCfun/baud search снова были возможны.
    void gsmNoteModemSoftReboot(bool cfunAlreadyDone);
    void gsmOpenUart(uint32_t baud);

    void onRxLine(const char* line);
    void handleUrc(const char* line);
    void handleAwaitLine(const char* line);
    /// Enqueue AT; returns false if AtSession queue full (await not armed).
    bool sendAt(const char* cmd, const char* tag, AwaitKind kind, uint32_t timeoutMs);
    /// Stop TCP and change to GPRS_ATTACH only when the AT bus is idle (C1 hangoff).
    void leaveReadyForReattach_(const char* why);
    void beginAwait(AwaitKind kind, uint32_t timeoutMs);
    bool awaitTimedOut(uint32_t now) const;
    void resetAwait();
    uint32_t gsmStallFingerprint() const;
    void gsmMaybeLogStallWatchdog(uint32_t now);
    /// Sole takeResult owner: TCP CIP* consume, else GSM await absorb.
    void drainAtResult_();
    void gsmAbsorbAtSessionResult(const AtSession::Result& r);

    // Работа с буфером ответа
    void clearResponse();
    void appendResponseChar(char c);
    bool responseContains(const char* needle) const;

    // Смена состояния
    void changeState(GSMState newState);

    /// Clear sticky GSM bring-up errors (NO_RESPONSE / REG_FAIL / APN_FAIL) on READY.
    void clearGsmBringupErrors_();
    /// Prefer REG_FAIL when deregistered or no/weak CSQ; else APN_FAIL.
    ErrorCode classifyBearerFail_() const;

    // Отправка AT-команды
    void sendCommand(const char* cmd);

    // Обновить уровень сигнала
    void updateSignalQuality();

    // Прочитать оператора
    void readOperator();

    // Обработчики состояний
    void handleIdle();
    void handleInit();
    void handleRegistering();
    void handleGprsSetup();
    void handleGprsAttach();
    void handleGprsGetIp();
    void handleReady();
    void handleError();

    /// Snapshot Contype/APN/USER/PWD last pushed in GPRS_SETUP (warm-resume / live change).
    void snapshotAppliedApn_();
    bool apnMatchesApplied_() const;
    void clearAppliedApn_();

    void serviceTimeSync(uint32_t now);
    void timeFailStep_(const char* why);
    void timeFinishOk_(time_t epochUtc, TimeSource src);
    void timeAdvanceToCipgsmloc_(uint32_t now);
    void timeAdvanceToCntpOrFail_(uint32_t now, const char* why);

    friend class GsmInitFsm;
};

