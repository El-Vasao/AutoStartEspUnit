#pragma once

// Host stand-in for HardwareSerial: records TX, lets tests inject RX bytes.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "Print.h"

#ifndef SERIAL_8N1
#define SERIAL_8N1 0x800001c
#endif

class HardwareSerial : public Stream {
public:
    static constexpr size_t kCap = 2048;

    void begin(unsigned long baud, uint32_t = SERIAL_8N1, int8_t = -1, int8_t = -1) { baud_ = baud; }
    void end() {}
    unsigned long baud() const { return baud_; }

    size_t write(uint8_t c) override {
        if (txLen_ + 1 >= kCap) return 0;
        tx_[txLen_++] = (char)c;
        tx_[txLen_] = '\0';
        return 1;
    }
    size_t write(const uint8_t* b, size_t n) override {
        size_t w = 0;
        while (w < n && write(b[w])) w++;
        return w;
    }
    using Print::write;

    int available() override { return (int)(rxLen_ - rxPos_); }
    int read() override { return rxPos_ < rxLen_ ? (uint8_t)rx_[rxPos_++] : -1; }
    int peek() override { return rxPos_ < rxLen_ ? (uint8_t)rx_[rxPos_] : -1; }
    void flush() {}

    // --- test helpers ---
    void injectRx(const char* s) { injectRx(reinterpret_cast<const uint8_t*>(s), strlen(s)); }
    void injectRx(const uint8_t* b, size_t n) {
        if (rxPos_ == rxLen_) rxPos_ = rxLen_ = 0;
        for (size_t i = 0; i < n && rxLen_ < kCap; i++) rx_[rxLen_++] = (char)b[i];
    }
    const char* tx() const { return tx_; }
    void clearTx() {
        txLen_ = 0;
        tx_[0] = '\0';
    }

private:
    unsigned long baud_{0};
    char tx_[kCap]{};
    size_t txLen_{0};
    char rx_[kCap]{};
    size_t rxLen_{0};
    size_t rxPos_{0};
};
