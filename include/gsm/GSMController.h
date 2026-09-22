// include/gsm/GSMController.h
#pragma once

#include <Arduino.h>
#include "common/Constants.h"
#include "gsm/GsmModemStack.h"
#include "gsm/GsmInitPhase.h"

// Состояния GSM-модема
enum class GSMState : uint8_t {
    IDLE,                ///< ожидание
    INIT,                ///< инициализация (AT-команды)
    REGISTERING,         ///< регистрация в сети
    GPRS_SETUP,          ///< настройка APN (AT+CSTT)
    GPRS_ATTACH,         ///< прикрепление к GPRS (AT+CGATT=1)
    GPRS_ACTIVATE,       ///< активация контекста PDP (AT+CIICR)
    GPRS_GETIP,          ///< получение IP-адреса (AT+CIFSR)
    READY,               ///< модем готов, GPRS подключён
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

    // Проверить, готов ли модем к работе (GPRS подключён)
    bool isReady() const { return _state == GSMState::READY; }

    // Получить название оператора
    const char* getOperator() const { return _operator; }

    // Получить уровень сигнала (0-31)
    int16_t getSignalQuality() const { return _signal; }

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

    /// True while TCP connect/send/close/recover holds the shared AT bus.
    bool tcpBusBusy() const { return _stack.tcp.isBusBusy(); }

    /// Skip MQTT RX while modem TX/CIPSEND owns the bus or during post-send quiet.
    bool shouldDeferMqttRead() const { return _stack.tcp.shouldDeferMqttRead(); }

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
    uint32_t _postLockQuietUntilMs{0};
    uint32_t _waitFirstUntilMs{0};
    uint8_t _waitFirstStep{0};

    /// Липкий до `stop()`: хоть раз успешно пройден шаг `AT`+`AT+CGMI` (SIMCOM) в этой GSM‑сессии.
    bool _verifiedModemContactSinceStop{false};
    uint32_t _firstAtFallbackStartMs{0};
    /// Не раньше этого millis() слать следующий hypothesis AT (spaced backoff).
    uint32_t _hypNextAttemptMs{0};
    /// Подрежим INIT (hypothesis AT/CGMI, baud search, IPR NV, resume CREG..., modem policy см. GSMController.Fsm.Init.cpp).
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
    void sendAt(const char* cmd, const char* tag, AwaitKind kind, uint32_t timeoutMs);
    void beginAwait(AwaitKind kind, uint32_t timeoutMs);
    bool awaitTimedOut(uint32_t now) const;
    void resetAwait();
    uint32_t gsmStallFingerprint() const;
    void gsmMaybeLogStallWatchdog(uint32_t now);
    void gsmAbsorbAtSessionResult(const AtSession::Result& r);

    // Работа с буфером ответа
    void clearResponse();
    void appendResponseChar(char c);
    bool responseContains(const char* needle) const;

    // Смена состояния
    void changeState(GSMState newState);

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
    void handleGprsActivate();
    void handleGprsGetIp();
    void handleReady();
    void handleError();
};

