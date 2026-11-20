#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

/// Адаптер Print: пересылает каждую запись и считает байты (замер и кодирование за один проход).
struct CountingForwarder final : public Print {
    Print& d;
    size_t n = 0;
    explicit CountingForwarder(Print& x) : d(x) {}

    size_t write(uint8_t b) override {
        const size_t w = d.write(b);
        n += w;
        return w;
    }
    size_t write(const uint8_t* buf, size_t s) override {
        const size_t w = d.write(buf, s);
        n += w;
        return w;
    }
    size_t written() const { return n; }
};
