#pragma once

#include <stdint.h>

// Minimal stub for Constants.h WebConfig::AP_IP / AP_NETMASK on native.
class IPAddress {
public:
    IPAddress() : _a(0), _b(0), _c(0), _d(0) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
        : _a(a), _b(b), _c(c), _d(d) {}

    uint8_t _a, _b, _c, _d;
};
