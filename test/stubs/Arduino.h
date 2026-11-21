#pragma once

// Минимальные заглушки хоста, чтобы заголовки прошивки собирались в [env:native].
// Время задают тесты: test_clock::set(ms) / test_clock::advance(ms).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <algorithm>
using std::max;
using std::min;

#include "Print.h"
#include "HardwareSerial.h"

extern HardwareSerial Serial;

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PSTR
#define PSTR(s) (s)
#endif
#ifndef F
#define F(s) (s)
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#endif
#ifndef memcpy_P
#define memcpy_P(dest, src, n) memcpy((dest), (src), (n))
#endif
#ifndef strcmp_P
#define strcmp_P(a, b) strcmp((a), (b))
#endif
#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif
#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#ifndef HIGH
#define HIGH 0x1
#endif
#ifndef LOW
#define LOW 0x0
#endif
#ifndef INPUT
#define INPUT 0x0
#endif
#ifndef OUTPUT
#define OUTPUT 0x1
#endif
#ifndef INPUT_PULLUP
#define INPUT_PULLUP 0x2
#endif

using boolean = bool;
using byte = uint8_t;
using word = uint16_t;

namespace test_clock {
inline unsigned long& ms() {
    static unsigned long v = 0;
    return v;
}
inline unsigned long& us() {
    static unsigned long v = 0;
    return v;
}
inline void set(unsigned long m) {
    ms() = m;
    us() = m * 1000UL;
}
inline void advance(unsigned long d) { set(ms() + d); }
} // пространство имён test_clock

inline unsigned long millis() { return test_clock::ms(); }
inline unsigned long micros() { return test_clock::us(); }
// Стресс-тест гоняет delay() из producer-потока: в этот момент часы не двигаем (таймаут кольца не от планировщика).
inline bool& delayAdvancesTestClock() {
    static bool v = true;
    return v;
}
inline void delay(unsigned long d) {
    if (delayAdvancesTestClock()) test_clock::advance(d);
}
inline void yield() {}

// В MinGW и glibc < 2.38 нет strlcpy/strlcat.
#if !defined(__APPLE__) && !(defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 38)))
inline size_t strlcpy(char* dst, const char* src, size_t size) {
    const size_t n = strlen(src);
    if (size) {
        const size_t c = n < size - 1 ? n : size - 1;
        memcpy(dst, src, c);
        dst[c] = '\0';
    }
    return n;
}
inline size_t strlcat(char* dst, const char* src, size_t size) {
    const size_t d = strnlen(dst, size);
    if (d == size) return size + strlen(src);
    return d + strlcpy(dst + d, src, size - d);
}
#endif
