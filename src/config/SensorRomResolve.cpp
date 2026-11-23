#include "config/SensorRomResolve.h"

#include <stdio.h>
#include <string.h>

#include "common/Constants.h"

void formatSensorRomString(const uint8_t* addr8, char* out, size_t outSz) {
    if (!out || outSz == 0) return;
    out[0] = '\0';
    if (!addr8) return;
    snprintf(out, outSz, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", addr8[0], addr8[1], addr8[2], addr8[3],
             addr8[4], addr8[5], addr8[6], addr8[7]);
}

bool parseSensorRomString(const char* rom, uint8_t out[8]) {
    if (!rom || !out || !rom[0]) return false;
    unsigned b0, b1, b2, b3, b4, b5, b6, b7;
    // Ровно 8 hex-байт через ':' — иначе отказ (хвост после HH тоже отказ).
    char trail = '\0';
    const int n = sscanf(rom, "%2x:%2x:%2x:%2x:%2x:%2x:%2x:%2x%c", &b0, &b1, &b2, &b3, &b4, &b5, &b6, &b7,
                         &trail);
    if (n != 8) return false;
    if (b0 > 0xFFu || b1 > 0xFFu || b2 > 0xFFu || b3 > 0xFFu || b4 > 0xFFu || b5 > 0xFFu ||
        b6 > 0xFFu || b7 > 0xFFu) {
        return false;
    }
    out[0] = static_cast<uint8_t>(b0);
    out[1] = static_cast<uint8_t>(b1);
    out[2] = static_cast<uint8_t>(b2);
    out[3] = static_cast<uint8_t>(b3);
    out[4] = static_cast<uint8_t>(b4);
    out[5] = static_cast<uint8_t>(b5);
    out[6] = static_cast<uint8_t>(b6);
    out[7] = static_cast<uint8_t>(b7);
    return true;
}

uint8_t resolveSensorConfigId(const uint8_t* addr8, const BaseConfig& cfg) {
    if (!addr8) return 0;
    char romStr[TextBytes::Sensors::ADDR_STRING];
    formatSensorRomString(addr8, romStr, sizeof(romStr));
    for (uint8_t j = 0; j < HardwareLimits::SENSORS; j++) {
        const auto& sc = cfg.sensors[j];
        if (sc.rom[0] == '\0' || sc.id == 0) continue;
        if (strcmp(sc.rom, romStr) == 0) return sc.id;
    }
    return 0;
}
