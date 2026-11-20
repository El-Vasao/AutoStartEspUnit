// include/gsm/GsmController.h
#pragma once

#include <Arduino.h>
#include <time.h>
#include "common/Constants.h"
#include "common/ErrorCodes.h"
#include "gsm/GsmModemStack.h"
#include "gsm/GsmInitPhase.h"

// Состояния GSM-модема
enum class GsmState : uint8_t {
    IDLE,                ///< ожидание
    INIT,                ///< инициализация (AT-команды)
    REGISTERING,         ///< регистрация в сети (AT+CREG?)
    GPRS_SETUP,          ///< настройка SAPBR Contype/APN/USER/PWD
    GPRS_ATTACH,         ///< сначала статус SAPBR (опрос 2,1 → открытие 1,1)
    GPRS_GETIP,          ///< подтверждение IP (AT+SAPBR=2,1)
    READY,               ///< модем готов, bearer поднят
    ERROR                ///< ошибка, требуется перезапуск
};

/**
 * @brief Контроллер GSM-модема SIM800.
 * Реализует асинхронный конечный автомат для регистрации и подключения GPRS.
 * Используется как поставщик транспортной готовности для MQTT.
 *
 * Важно по железу:
 * - по умолчанию UART0 (`Serial`) используется для AT-трафика с модемом (не для `logger`).
 *   Логи идут в SSE (UI) и не должны вмешиваться в UART0.
 *
 * Важно по памяти:
 * - RX ответа модема — кольцевой буфер `char[_responseBuffer]` фиксированного размера (без Arduino `String`).
 */
class GsmController {
    friend struct GsmVoiceTestAccess;

public:
    enum class VoiceKind : uint8_t { None = 0, OutCall, OutSms, Inbound };
    enum class VoicePhase : uint8_t {
        Idle = 0,
        NeedMqttDrain,
        StopTcp,
        DialSend,
        DialWaitOk,
        DialRing,
        HangupSend,
        HangupWait,
        CmgsCmgfSend, ///< AT+CMGF=1 перед CMGS: init глотает ошибку текстового режима
        CmgsCmgfWait,
        CmgsSend,
        CmgsWaitPrompt,
        CmgsSendBody,
        CmgsWaitOk,
        CmgsAbortEsc, ///< ESC из текстового режима SMS перед FinishFail
        AnswerSend,
        AnswerWait,
        CollectPassword,
        CollectProgId,
        FinishOk,
        FinishFail,
    };

    GsmController();

    // Запуск/перезапуск автомата (после загрузки конфига)
    void begin();

    // Остановить автомат и сбросить рабочее состояние
    void stop();

    // Обновление состояния (вызывать в loop)
    void update();
    void requestReattach() { _reattachRequested = true; }
    void requestModemReboot() { _userRebootRequested = true; }
    /// Сбросить паузу reattach после устойчивой сессии данных MQTT (не при каждом входе в READY).
    void clearReattachBackoff();

    // Проверить, готов ли модем к работе (GPRS подключён)
    bool isReady() const { return _state == GsmState::READY; }

    // Получить название оператора
    const char* getOperator() const { return _operator; }

    // Получить уровень сигнала (0-31, 99 = неизвестно)
    int16_t getSignalQuality() const { return _signal; }

    // BER из последнего AT+CSQ (0..7), или -1 если ещё не было ответа
    int16_t getSignalBer() const { return _signalBer; }

    // Возраст текущего состояния (мс).
    uint32_t getStateAgeMs() const { return millis() - _stateStartTime; }

    // Текущее состояние в виде строки
    const char* getStateString() const;

    // Текущее состояние (перечисление)
    GsmState getState() const { return _state; }

    // Получить ссылку на клиент для MQTT
    Client& getClient() { return _stack.client; }

    /// Пока активна эпоха CIP TCP: отправка/закрытие/восстановление (не обычный AT CSQ/COPS).
    bool tcpEpochBusy() const { return _stack.tcp.isTcpEpochBusy(); }

    /// Байт в подготовке CIPSEND или эпоха отправки до SEND OK (без CIPCLOSE/CIPSTART).
    bool tcpSendBusy() const { return _stack.tcp.hasBufferedTx(); }
    /// Эпоха или любая занятость AtSession — для reattach/слива.
    bool tcpBusBusy() const { return _stack.tcp.isBusBusy(); }

    /// Пока сокет CIP TCP поднят или подключается (NTP запускать нельзя).
    bool tcpSocketActive() const {
        return _stack.tcp.isConnected() || _stack.tcp.isConnecting();
    }

    /// Липкое переполнение кольца RX TCP (MQTT должен переподключиться).
    bool takeTcpRxOverflow() { return _stack.tcp.takeRxOverflow(); }

    /// Журнал снимка Sim800 +IPD / эпохи отправки (разбор MQTT rx_incomplete).
    void logTcpRxForensic(const char* why) { _stack.tcp.logRxForensic(why); }

    /// Кто дал последнюю успешную синхронизацию времени модема (статус/отладка).
    enum class TimeSource : uint8_t { None = 0, Cclk, Cipgsmloc, Cntp };

    /// Биты для takeTimeSyncFail, если каскад закончился без epoch.
    enum TimeFailBit : uint8_t {
        TimeFailCclk = 1u << 0,
        TimeFailCipgsmloc = 1u << 1,
        TimeFailCntp = 1u << 2,
    };

    /// Шаги каскада часов (публичные, чтобы проверку занятости можно было встроить).
    enum class TimeStep : uint8_t {
        Idle = 0,
        CclkProbe,
        Cipshut,           ///< AT+CIPSHUT перед шагами, которым нужен IP (после MQTT CIPCLOSE)
        Cipgsmloc,
        WaitCipgsmlocUrc,  ///< +CIPGSMLOC часто приходит после OK
        Cntpcid,
        CntpSet,
        CntpRun,
        WaitCntpUrc,
        CclkAfterCntp,
    };

    /// Неблокирующий каскад: CCLK/NITZ → CIPSHUT → CIPGSMLOC → CNTP. false, если занято / не готов / TCP активен.
    /// `ntpServer` может быть пустым — тогда CNTP пропускается после неудачи CCLK/CIPGSMLOC.
    bool requestTimeSync(const char* ntpServer, int8_t tzOffsetHours);
    /// Только AT-опрос CCLK; допустим, пока MQTT CIP поднят. Устаревшие часы → мягкий промах (без маски ошибки).
    bool requestCclkProbe(int8_t tzOffsetHours);
    bool timeSyncBusy() const { return _timeStep != TimeStep::Idle; }
    /// Тяжёлый каскад владеет CIP (не лёгкий CCLK) — отложить reattach / не пускать подключение MQTT.
    bool timeHeavySyncBusy() const {
        return _timeStep != TimeStep::Idle && !_timeCclkOnly;
    }
    /// Забирает успешный результат синхронизации (epoch UTC). false, если ничего не ждёт.
    /// Если CCLK принёс целый час пояса, `tzValidOut` истинен и `tzHoursOut` задан.
    bool takeTimeEpochUtc(time_t& epochUtcOut, TimeSource* sourceOut = nullptr,
                          int8_t* tzHoursOut = nullptr, bool* tzValidOut = nullptr);
    /// Забирает конечную неудачу каскада. `failMaskOut` = ИЛИ битов TimeFailBit. false, если ничего нет.
    bool takeTimeSyncFail(uint8_t& failMaskOut);

    /// Эксклюзивная эпоха голоса/SMS (MQTT CIP должен быть опущен). Исходящие и входящие.
    bool modemServiceEpochBusy() const { return _voicePhase != VoicePhase::Idle; }
    /// CellularCore: слить MQTT, затем вызвать notifyMqttDrainedForServiceEpoch().
    bool serviceEpochNeedsMqttDrain() const { return _voicePhase == VoicePhase::NeedMqttDrain; }
    void notifyMqttDrainedForServiceEpoch();
    /// Прерывание/остановка программы: положить трубку или отменить эпоху, чтобы ATD/CMGS не остались без владельца.
    void abortModemServiceEpoch();

    /// Исходящий вызов на owner_phone. Звонит до ответа или `Gsm::VOICE_DEFAULT_RING_MS`, затем ATH.
    /// false, если отклонено.
    bool requestCallOwner();
    bool requestSmsOwner(const char* message);
    /// Забирает итог исходящей операции. false, если ещё не готов.
    bool takeVoiceOpResult(bool& okOut);

    /// Входящий DTMF: id программы к запуску (один раз). false, если нет.
    bool takePendingProgramStart(uint8_t& programIdOut);

private:
    static constexpr size_t RESPONSE_BUF_SIZE = Gsm::RESPONSE_BUFFER_SIZE;
    static constexpr size_t AT_CMD_BUF_SIZE = Gsm::CMD_BUFFER_SIZE;

    HardwareSerial* _serial;        ///< указатель на последовательный порт (Serial)
    GsmModemStack _stack;

    GsmState _state;                 ///< текущее состояние
    uint32_t _baud;                  ///< найденная/используемая скорость UART
    uint32_t _stateStartTime;        ///< время входа в текущее состояние (мс)
    uint32_t _lastCommandTime;       ///< время последней отправленной команды (мс)
    uint32_t _lastRxByteMs{0};       ///< время последнего принятого байта (мс), для диагностического сброса
    // RX буфер ответа модема (кольцевой буфер). Нужен для простых проверок вхождения (OK/ERROR/+CREG...).
    // Важно: без memmove/heap, чтобы выдерживать длинные сессии и шумный UART.
    char _responseBuffer[RESPONSE_BUF_SIZE];
    uint16_t _respHead{0};           ///< индекс первого (самого старого) байта
    uint16_t _respCount{0};          ///< сколько байт валидно в буфере (0..RESPONSE_BUF_SIZE)
    // Разбор строк RX делает ModemUart.
    char _operator[TextBytes::Gsm::OPERATOR]; ///< название оператора
    int16_t _signal;                 ///< уровень сигнала
    int16_t _signalBer{-1};          ///< BER из AT+CSQ (0..7) или -1, неизвестно
    uint8_t _retryCount;             ///< счётчик повторных попыток
    bool _reattachRequested{false};
    bool _userRebootRequested{false};
    uint8_t _rebootStep{0};
    uint32_t _reattachCooldownUntilMs{0};
    uint8_t _reattachBackoffStep{0};

    // --- URC/парсинг строк ---
    int8_t _cregStat{-1};            ///< -1 неизвестно, иначе stat из +CREG
    int8_t _cgattStat{-1};           ///< -1 неизвестно, иначе 0/1 из +CGATT
    bool _pdpDeactSeen{false};
    bool _closedSeen{false};
    uint32_t _tcpClosedFirstMs{0};
    uint8_t _tcpClosedStreak{0};

    // --- Ожидание ответа на текущую команду ---
    enum class AwaitKind : uint8_t { NONE, OK, CREG, IP };
    AwaitKind _await{AwaitKind::NONE};
    uint32_t _awaitDeadlineMs{0};
    bool _awaitOk{false};
    bool _awaitError{false};
    bool _awaitGotCreg{false};
    bool _awaitGotIp{false};
    uint16_t _cmdId{0};
    uint16_t _awaitCmdId{0};
    uint8_t _gprsCfgStep{0};
    /// GPRS_ATTACH: 0=опрос SAPBR=2,1, 1=открытие SAPBR=1,1, 2=повторный опрос после неудачи открытия.
    uint8_t _attachProbeStep{0};
    char _atCmdBuf[AT_CMD_BUF_SIZE]{}; ///< буфер текущей поставленной AT-команды (AtSession держит невладеющие указатели)

    // Здоровье подъёма
    uint32_t _bringupStartMs{0};
    uint8_t _recoveryLevel{0};
    uint32_t _cooldownUntilMs{0};
    bool _restartPending{false};
    /// ERROR L2/L3: ждём CIPSHUT / SAPBR0, прежде чем запустить begin().
    bool _errorRecoveryCmdPending{false};
    /// Завершённые циклы L1→L4 с последнего READY; потолок — ERROR_RECOVERY_MAX_CYCLES.
    uint8_t _errorRecoveryCycles{0};
    uint32_t _postLockQuietUntilMs{0};
    uint32_t _waitFirstUntilMs{0};

    /// Липкий до `stop()`: хоть раз успешно пройден шаг `AT`+`AT+CGMI` (SIMCOM) в этой GSM‑сессии.
    bool _verifiedModemContactSinceStop{false};
    uint32_t _firstAtFallbackStartMs{0};
    /// Не раньше этого millis() слать следующий hypothesis AT (пауза между попытками).
    uint32_t _hypNextAttemptMs{0};
    /// Подрежим INIT (hypothesis AT/CGMI, поиск скорости, IPR NV, возобновление CREG…; см. GsmController.Fsm.Init.cpp).
    GsmInitPhase _initPhase{GsmInitPhase::None};
    bool _baudSearchActive{false};
    uint8_t _baudSearchRound{0};
    uint8_t _baudSearchBaudIdx{0};
    uint32_t _baudSearchDeadlineMs{0};
    uint32_t _baudCooldownUntilMs{0};
    bool _didIprNvProbeThisCycle{false};
    /// Один `AT+CFUN=1,1` по возможности на цикл до поиска скорости (или уже сделан UI/ERROR L4).
    bool _didPreBaudSearchCfun{false};
    bool _resumeSapbrHadIp{false};
    GsmState _postResumeTarget{GsmState::REGISTERING};
    /// Счётчик повторов `AT+CGATT?` в INIT (SIM busy до CPIN READY).
    uint8_t _initCgattBusyRetries{0};

    /// Последние учётные данные APN, записанные через SAPBR=3 (пусто ⇒ в этой сессии ещё не применялись).
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
    static constexpr uint8_t EVENT_RING_SIZE = Gsm::EVENT_RING_SIZE;
    GsmEvent _ev[EVENT_RING_SIZE]{};
    uint8_t _evHead{0};
    void ev(uint8_t type, uint16_t aux = 0);

    uint32_t _lastDiagMs{0};
    uint32_t _lastOperatorMs{0};

    /// Каскад часов (CCLK → CIPGSMLOC → CNTP) — двигается из handleReady, когда шина AT свободна.
    TimeStep _timeStep{TimeStep::Idle};
    char _ntpServer[TextBytes::TimeCfg::NTP_SERVER]{};
    int8_t _ntpTzQuarters{0};
    bool _ntpUrcOk{false};
    bool _ntpUrcFail{false};
    bool _timeResultReady{false};
    bool _timeCmdSent{false};
    time_t _timeEpochUtc{0};
    TimeSource _timeSource{TimeSource::None};
    int8_t _timeTzHours{0};
    bool _timeTzValid{false};
    uint8_t _timeFailMask{0};
    bool _timeFailReady{false};
    uint32_t _timeDeadlineMs{0};
    char _cclkSnap[48]{};
    char _cipgsmlocSnap[64]{};
    /// Если истина, CclkProbe не переходит к CIPGSMLOC (лёгкий путь при поднятом MQTT).
    bool _timeCclkOnly{false};

    /// Эксклюзивная эпоха голоса/SMS (CALL_OWNER, SMS_OWNER, входящий DTMF).
    VoiceKind _voiceKind{VoiceKind::None};
    VoicePhase _voicePhase{VoicePhase::Idle};
    char _voicePhone[TextBytes::Gsm::PHONE]{};
    char _voiceSmsText[TextBytes::Programs::STEP_MESSAGE]{};
    char _voiceDtmfBuf[TextBytes::Gsm::DTMF_PASSWORD]{};
    char _voiceProgDigits[4]{};
    uint8_t _voiceDtmfLen{0};
    uint8_t _voiceProgDigitLen{0};
    uint32_t _voiceTimeoutMs{0};
    uint32_t _voiceEpochStartMs{0};
    uint32_t _voicePhaseDeadlineMs{0};
    uint32_t _voiceHangupDeadlineMs{0};
    bool _voiceResultReady{false};
    bool _voiceResultOk{false};
    bool _voicePromptSeen{false};
    bool _voiceNoCarrier{false};
    /// DialRing: +CLCC stat 0 — удалённая сторона сняла трубку.
    bool _voiceAnswered{false};
    uint32_t _voiceNextClccMs{0};
    bool _voicePendingProgramValid{false};
    uint8_t _voicePendingProgramId{0};
    /// Входящий: видели RING, ждём подходящий +CLIP перед стартом эпохи.
    bool _inboundRingSeen{false};
    uint32_t _inboundClipDeadlineMs{0};
    char _inboundClipDigits[24]{};

    /// Редкая диагностика «застряли»: снимок state/init/await не меняется дольше интервала.
    uint32_t _gsmStallPrevFp{0};
    uint32_t _gsmStallFpSinceMs{0};
    uint32_t _gsmStallLastLogMs{0};

    void flushInput();
    void logRxSnippet(const char* tag) const;
    bool gsmCgmiResponseManufacturerOk() const;
    bool gsmParseStoredIprBaud(uint32_t& baudOut) const;
    void gsmResetSessionAfterStop();
    /// После мягкой перезагрузки модема: снять липкую проверку, чтобы PreCfun/поиск скорости снова были возможны.
    void gsmNoteModemSoftReboot(bool cfunAlreadyDone);
    void gsmOpenUart(uint32_t baud);

    void onRxLine(const char* line);
    void handleUrc(const char* line);
    void handleAwaitLine(const char* line);
    /// Поставить AT в очередь; false, если очередь AtSession полна (await не включён).
    bool sendAt(const char* cmd, const char* tag, AwaitKind kind, uint32_t timeoutMs);
    /// Остановить TCP и перейти в GPRS_ATTACH только когда шина AT свободна (отложенный уход C1).
    void leaveReadyForReattach_(const char* why);
    void beginAwait(AwaitKind kind, uint32_t timeoutMs);
    bool awaitTimedOut(uint32_t now) const;
    void resetAwait();
    uint32_t gsmStallFingerprint() const;
    void gsmMaybeLogStallWatchdog(uint32_t now);
    /// Единственный владелец takeResult: сначала TCP забирает CIP*, иначе GSM впитывает в await.
    void drainAtResult_();
    void gsmAbsorbAtSessionResult(const AtSession::Result& r);

    // Работа с буфером ответа
    void clearResponse();
    void appendResponseChar(char c);
    bool responseContains(const char* needle) const;

    // Смена состояния
    void changeState(GsmState newState);

    /// Сбросить липкие ошибки подъёма GSM (NO_RESPONSE / REG_FAIL / APN_FAIL) при READY.
    void clearGsmBringupErrors_();
    /// REG_FAIL, если нет регистрации или CSQ нет/слабый; иначе APN_FAIL.
    ErrorCode classifyBearerFail_() const;

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

    /// Снимок Contype/APN/USER/PWD, последний раз отправленных в GPRS_SETUP (тёплое возобновление / смена на лету).
    void snapshotAppliedApn_();
    bool apnMatchesApplied_() const;
    void clearAppliedApn_();

    void serviceTimeSync(uint32_t now);
    void timeFailStep_(const char* why);
    void timeFinishOk_(time_t epochUtc, TimeSource src, bool tzValid = false, int8_t tzHours = 0);
    void timeMarkMethodFail_(uint8_t bit);
    void timeFailTerminal_(const char* why);
    void timeAdvanceToCipgsmloc_(uint32_t now);
    void timeEnterCipgsmlocCmd_(uint32_t now);
    void timeAdvanceToCntpOrFail_(uint32_t now, const char* why);
    void timeSoftMissCclkLite_(const char* why);

    void serviceVoice_(uint32_t now);
    /// Фазы AT+CMGS со своим бюджетом: голосовой потолок 60 с их не режет.
    bool voiceSmsOnWire_() const;
    void voiceReset_();
    void voiceFinish_(bool ok);
    void voiceAbortInboundClipWait_();
    bool voiceTryBeginInbound_(const char* clipDigits);
    void voiceOnDtmfTone_(char tone);
    void voiceHandleUrc_(const char* line);
};

