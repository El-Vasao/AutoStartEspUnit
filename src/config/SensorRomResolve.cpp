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

uint16_t resolveSensorConfigId(const uint8_t* addr8, const BaseConfig& cfg) {
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
