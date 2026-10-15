#pragma once

// Host stand-in for the Arduino Print / Stream / Client hierarchy (subset used by firmware code).
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

class Print {
public:
    virtual ~Print() = default;

    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t* buffer, size_t size) {
        size_t n = 0;
        while (size--) {
            if (write(*buffer++) == 0) break;
            n++;
        }
        return n;
    }
    size_t write(const char* s) { return s ? write(reinterpret_cast<const uint8_t*>(s), strlen(s)) : 0; }
    size_t write(const char* buffer, size_t size) { return write(reinterpret_cast<const uint8_t*>(buffer), size); }

    size_t print(const char* s) { return write(s); }
    size_t print(char c) { return write(static_cast<uint8_t>(c)); }
    size_t print(unsigned char v) { return printFmt("%u", static_cast<unsigned>(v)); }
    size_t print(int v) { return printFmt("%d", v); }
    size_t print(unsigned int v) { return printFmt("%u", v); }
    size_t print(long v) { return printFmt("%ld", v); }
    size_t print(unsigned long v) { return printFmt("%lu", v); }
    size_t print(long long v) { return printFmt("%lld", v); }
    size_t print(unsigned long long v) { return printFmt("%llu", v); }
    size_t print(double v, int digits = 2) {
        char fmt[8];
        snprintf(fmt, sizeof(fmt), "%%.%df", digits < 0 ? 0 : (digits > 9 ? 9 : digits));
        return printFmt(fmt, v);
    }
    size_t println() { return write(reinterpret_cast<const uint8_t*>("\r\n"), 2); }
    size_t println(const char* s) { return print(s) + println(); }

private:
    template <typename T>
    size_t printFmt(const char* fmt, T v) {
        char buf[48];
        const int n = snprintf(buf, sizeof(buf), fmt, v);
        return n > 0 ? write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(n)) : 0;
    }
};

class Stream : public Print {
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
};
