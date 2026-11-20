#pragma once

#include <Arduino.h>
#include "modem/ModemUart.h"
#include "modem/AtSession.h"
#include "modem/Sim800TcpTransport.h"

// Тонкий агрегатор IO модема, сессии AT и TCP-транспорта.
// Владение и время жизни явные, меньше полей в GsmController.
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

