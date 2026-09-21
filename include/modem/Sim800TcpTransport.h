#pragma once

#include <Arduino.h>
#include <Client.h>
#include "modem/AtSession.h"

// URC-first single-socket TCP transport for SIM800.
// - No CIPRXGET/CIPSTATUS polling
// - RX via +IPD,<len>:<data> parsed from raw bytes
class Sim800TcpTransport {
public:
    explicit Sim800TcpTransport(AtSession& at) : _at(at) {}

    void reset();
    void tick(uint32_t nowMs);
    /// Consume one AtSession result if it belongs to TCP (CIP*). Returns true if consumed.
    bool consumeAtResult(const AtSession::Result& r);

    // Hooks from modem UART
    void onLine(const char* line);
    void onByte(char c);

    bool isConnected() const { return _connected; }
    bool isConnecting() const { return _connecting; }
    /// True while connect/send/close/recover owns the shared AT/UART bus.
    bool isBusBusy() const {
        return _connecting || _sendInProgress || _txLen != 0 || _modemTxLocked || _closeQueued ||
               _recoverQueued || _at.isBusy();
    }

    bool connectStart(const char* host, uint16_t port);
    /// Clear stuck connect/send flags (e.g. Client::connect wall-clock timeout).
    void abandonConnect(const char* reason = nullptr);
    void stop(const char* reason = nullptr);

    // Non-blocking write: buffers data, schedules CIPSEND.
    size_t write(const uint8_t* data, size_t len);

    /// True while modem TX staging has bytes or CIPSEND is in flight (incl. post-'>' until SEND OK).
    bool hasBufferedTx() const { return _txLen != 0 || _sendInProgress || _modemTxLocked; }

    int available() const;
    int read();
    int peek() const;

    /// After repeated CIPSHUT recovers without CONNECT OK — GSM should reattach bearer.
    bool needsBearerReattach() const { return _needsBearerReattach; }
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
    /// Held from CIPSEND enqueue until SEND OK/FAIL/watchdog/recover done (survives AtSession Idle after '>').
    bool _modemTxLocked{false};
    bool _ipConfigDone{false};
    /// One-shot CIPSHUT before first CIPSTART after reset (warm modem / leftover socket).
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

    char _host[64]{};
    uint16_t _port{0};
    char _cmdStart[128]{};
    char _cmdSend[32]{};

    // RX ring buffer (compact; rely on frequent pumpRead in MQTT loop)
    static constexpr uint16_t RX_SIZE = 320;
    uint8_t _rx[RX_SIZE]{};
    uint16_t _rxHead{0};
    uint16_t _rxCount{0};

    // TX staging buffer (single packet)
    static constexpr uint16_t TX_SIZE = 224;
    uint8_t _tx[TX_SIZE]{};
    uint16_t _txLen{0};
    uint16_t _txSent{0};

    // +IPD parser state (byte-level)
    enum class IpState : uint8_t { Idle, MatchI, MatchP, MatchD, MatchComma, ReadLen, ReadData };
    IpState _ipState{IpState::Idle};
    uint16_t _ipLen{0};
    uint16_t _ipRead{0};

    // Raw push RX framing (CIPRXGET=0): modem may output network bytes directly, interleaved with text URCs/echo.
    // We sniff short CRLF-terminated text lines and drop known modem control lines, while passing binary through.
    static constexpr uint8_t RAW_LINE_BUF_SIZE = 64;
    char _rawLineBuf[RAW_LINE_BUF_SIZE]{};
    uint8_t _rawLineLen{0};

    // CIPSEND prompt '>' is consumed by AtSession for command completion but still arrives on the shared UART RX path.
    // Strip optional \r\n after it so the byte never enters the TCP/MQTT stream or the raw line sniffer.
    uint8_t _promptLeak{0}; // 0=none, 1=saw '>', 2=saw CR after '>'

    void pushRx_(uint8_t b);
    bool startConnect_();
    void startSend_();
    void clearTx_();
    void endSendEpoch_();
    void forceStackRecover_(const char* reason);
    void noteConnectOk_();
};

// Arduino Client adapter around Sim800TcpTransport
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
    void flush() override {}
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
