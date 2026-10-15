#pragma once

#include <Arduino.h>
#include "modem/ModemUart.h"
#include "modem/AtSession.h"
#include "modem/Sim800TcpTransport.h"

// Thin aggregator for modem IO + AT session + TCP transport.
// Keeps ownership/lifetimes explicit and reduces GsmController member clutter.
class GsmModemStack {
public:
    using PumpFn = Sim800ClientAdapter::PumpFn;
    explicit GsmModemStack(HardwareSerial& serial, PumpFn pump = nullptr, void* pumpCtx = nullptr)
        : uart(serial),
          at(uart),
          tcp(at),
          client(tcp, pump, pumpCtx) {}

    ModemUart uart;
    AtSession at;
    Sim800TcpTransport tcp;
    Sim800ClientAdapter client;
};

