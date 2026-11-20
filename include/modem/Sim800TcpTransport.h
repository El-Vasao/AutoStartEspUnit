#pragma once

#include <Arduino.h>
#include <Client.h>
#include "common/Constants.h"
#include "modem/AtSession.h"
#include "modem/Sim800Lines.h"

// TCP-транспорт SIM800: один сокет, разбор с URC.
// - Без опроса CIPRXGET/CIPSTATUS
// - RX через +IPD,<len>:<data>, разбор из сырых байт
class Sim800TcpTransport {
public:
    explicit Sim800TcpTransport(AtSession& at) : _at(at) {}

    void reset();
    void tick(uint32_t nowMs);
    /// Забрать один результат AtSession, если он про TCP (CIP*). true, если забран.
    bool consumeAtResult(const AtSession::Result& r);

    // Точки входа с UART модема
    void onLine(const char* line);
    void onByte(char c);

    bool isConnected() const { return _connected; }
    bool isConnecting() const { return _connecting; }
    /// Только эпоха TCP/CIP (CIPSTART/CIPSEND/CIPCLOSE/CIPSHUT) — без посторонних AT (CSQ/COPS/CNTP).
    bool isTcpEpochBusy() const {
        return _connecting || _sendInProgress || _txLen != 0 || _modemTxLocked || _closeQueued ||
               _recoverQueued;
    }
    /// Эпоха занята или любая команда AtSession в полёте (сериализация reattach/drain).
    bool isBusBusy() const { return isTcpEpochBusy() || _at.isBusy(); }

    bool connectStart(const char* host, uint16_t port);
    void stop(const char* reason = nullptr);

    /// Синхронизация времени (или другой владелец не TCP) выполнила AT+CIPSHUT — сбросить кэш прогретого CIPSTART / конфига IP.
    void noteExternalCipShut();

    // Неблокирующая запись: только в одну область подготовки CIPSEND (без автоотправки).
    size_t write(const uint8_t* data, size_t len);
    /// Зафиксировать подготовленные байты и начать AT+CIPSEND=N. Один раз на целый пакет MQTT/TCP.
    bool flushSend();

    /// Истина, пока в подготовке TX модема есть байты или CIPSEND в полёте (включая время после '>' до SEND OK).
    bool hasBufferedTx() const { return _txLen != 0 || _sendInProgress || _modemTxLocked; }

    /// Держится, пока не заберут: переполнение кольца RX (рассинхронизировало бы поток MQTT).
    bool takeRxOverflow();

    /// Снимок для разбора rx_incomplete MQTT / обрезки +IPD.
    struct RxForensic {
        uint8_t ipState{0};
        uint16_t ipLen{0};
        uint16_t ipRead{0};
        uint16_t rxCount{0};
        bool sendInProgress{false};
        bool modemTxLocked{false};
        bool connected{false};
        bool ipConfigDone{false};
    };
    void fillRxForensic(RxForensic& out) const;
    void logRxForensic(const char* why) const;

    int available() const;
    int read();
    int peek() const;

    /// После повторных восстановлений через CIPSHUT без CONNECT OK — GSM должен заново поднять bearer.
    bool consumeNeedsBearerReattach() {
        if (!_needsBearerReattach) return false;
        _needsBearerReattach = false;
        return true;
    }

private:
    AtSession& _at;

    bool _connected{false};
    bool _connecting{false};
    bool _sendInProgress{false};
    /// Держится от постановки CIPSEND в очередь до конца SEND OK/FAIL/watchdog/recover (живёт и после Idle AtSession за '>').
    bool _modemTxLocked{false};
    bool _ipConfigDone{false};
    /// Команды AT конфига поставлены в очередь; до CIPSTART ждать биты OK.
    bool _ipConfigEnqueued{false};
    /// Бит0=CIPRXGET0 OK, бит1=CIPHEAD1 OK, бит2=CIPMUX0 OK. Для Done нужно 0x07.
    uint8_t _ipConfigOkMask{0};
    /// Однократный CIPSHUT перед первым CIPSTART после сброса (прогретый модем / оставшийся сокет).
    bool _didInitialCipShut{false};
    bool _closeQueued{false};
    bool _recoverQueued{false};
    bool _needsBearerReattach{false};
    uint8_t _stackRecoverCount{0};
    uint32_t _lastNowMs{0};
    uint32_t _lastConnectOkMs{0};
    uint32_t _connectStartMs{0};
    uint32_t _sendWatchMs{0};
    uint32_t _lastStackRecoverMs{0};
    uint32_t _lastConnectAttemptMs{0};
    /// Буфер подготовки запечатан; write() отказывает, пока CIPSEND не завершится.
    bool _flushRequested{false};

    char _host[TextBytes::Mqtt::BROKER]{};
    uint16_t _port{0};
    char _cmdStart[Gsm::CMD_BUFFER_SIZE]{};
    char _cmdSend[32]{};

    // Кольцо RX (режим push); переполнение делает сеанс фатальным (старейшее не выбрасываем — это рассинхрон).
    static constexpr uint16_t RX_SIZE = Sim800Tcp::TCP_BUF_SIZE;
    uint8_t _rx[RX_SIZE]{};
    uint16_t _rxHead{0};
    uint16_t _rxCount{0};
    bool _rxOverflow{false};

    // Подготовка TX: одна эпоха CIPSEND размером под один целый пакет MQTT.
    static constexpr uint16_t TX_SIZE = Sim800Tcp::TCP_BUF_SIZE;
    uint8_t _tx[TX_SIZE]{};
    uint16_t _txLen{0};
    /// Длина, зафиксированная в AT+CIPSEND=N внутри startSend_ (должна совпасть с байтами на '>').
    uint16_t _sendLen{0};

    sim800_lines::IpdFramer _ipd;
    /// Последний байт сброса +IPD: dataMode снять на следующем байте, чтобы он не открыл строку.
    bool _ipdDiscardExit{false};

    // Сырое кадрирование push: ловить короткие управляющие строки CRLF; двоичное пропускать.
    static constexpr uint8_t RAW_LINE_BUF_SIZE = Sim800Tcp::RAW_LINE_BUF;
    char _rawLineBuf[RAW_LINE_BUF_SIZE]{};
    uint8_t _rawLineLen{0};

    // Снять утёкшее приглашение CIPSEND '>' (+ необязательные CR/LF), чтобы оно не попало в поток MQTT.
    uint8_t _promptLeak{0}; // 0=нет, 1=видели '>', 2=видели CR после '>'

    uint32_t nowMs_() const { return _lastNowMs ? _lastNowMs : millis(); }
    /// `fromIpd`: тело в кадре +IPD — оставлять в эпоху отправки (CONNACK/SUBACK). Сырой путь UART может быть мусором.
    void pushRx_(uint8_t b, bool fromIpd = false);
    /// Выбрасывать байты не из IPD, пока UART занят CIPSEND.
    bool discardingTcpPayload_(bool fromIpd) const;
    /// Фильтр управляющих строк CRLF / RX MQTT (без распознавателя +IPD). После повтора байт сорванного сопоставления.
    void feedRawByte_(char c);
    bool startConnect_();
    bool enqueueIpConfig_();
    bool enqueueCipStart_();
    void startSend_();
    void clearTx_();
    void endSendEpoch_();
    void forceStackRecover_(const char* reason);
    void noteConnectOk_();
    void clearIpConfigFlags_();
};

// Адаптер Arduino Client вокруг Sim800TcpTransport
class Sim800ClientAdapter : public Client {
public:
    using PumpFn = void (*)(void* ctx);

    explicit Sim800ClientAdapter(Sim800TcpTransport& t, PumpFn pump = nullptr, void* pumpCtx = nullptr)
        : _t(t), _pump(pump), _pumpCtx(pumpCtx) {}

    int connect(IPAddress ip, uint16_t port) override;
    int connect(const char* host, uint16_t port) override;
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t* buf, size_t size) override;
    int available() override {
        pump_();
        return _t.available();
    }
    int read() override {
        pump_();
        return _t.read();
    }
    int read(uint8_t* buf, size_t size) override;
    int peek() override {
        pump_();
        return _t.peek();
    }
    void flush() override {
        pump_();
        (void)_t.flushSend();
        pump_();
    }
    void stop() override { _t.stop(); }
    void stop(const char* reason) { _t.stop(reason); }
    uint8_t connected() override {
        pump_();
        return _t.isConnected() ? 1 : 0;
    }
    operator bool() override { return true; }

private:
    Sim800TcpTransport& _t;
    PumpFn _pump{nullptr};
    void* _pumpCtx{nullptr};

    void pump_() {
        if (_pump) _pump(_pumpCtx);
    }
};
