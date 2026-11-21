#pragma once

// Счётчики Update и переключение boot в end — как у Arduino-ESP32:
// end пишет новый слот в otadata, abort после end слот не возвращает.
#include "esp_ota_ops.h"

#include <stddef.h>
#include <stdint.h>

struct OtaUpdateCounters {
    int beginN{0};
    int writeN{0};
    int endN{0};
    int abortN{0};
    bool begun{false};
    size_t size{0};
    size_t written{0};
    uint32_t check{0};  // хэш байтов в порядке записи: проверка целостности потока
};

inline OtaUpdateCounters& otaUpdateCounters() {
    static OtaUpdateCounters counters;
    return counters;
}

namespace ota_stub {
inline int beginCount() { return otaUpdateCounters().beginN; }
inline int writeCount() { return otaUpdateCounters().writeN; }
inline int endCount() { return otaUpdateCounters().endN; }
inline int abortCount() { return otaUpdateCounters().abortN; }
inline size_t writtenBytes() { return otaUpdateCounters().written; }
inline uint32_t checksum() { return otaUpdateCounters().check; }

inline void reset() {
    otaUpdateCounters() = OtaUpdateCounters{};
    esp_ota_test_reset();
}
} // пространство имён ota_stub

class UpdateClass {
public:
    bool begin(size_t size, int = 0, int = -1, uint8_t = 0, const char* = nullptr) {
        auto& c = otaUpdateCounters();
        c.beginN++;
        c.begun = true;
        c.size = size;
        c.written = 0;
        c.check = 0;
        return true;
    }

    size_t write(uint8_t* data, size_t len) {
        auto& c = otaUpdateCounters();
        c.writeN++;
        if (!c.begun || !data) return 0;
        for (size_t i = 0; i < len; i++) c.check = c.check * 131u + data[i];
        c.written += len;
        return len;
    }

    bool end(bool evenIfRemaining = false) {
        auto& c = otaUpdateCounters();
        c.endN++;
        if (!c.begun) return false;
        if (!evenIfRemaining && c.written != c.size) return false;
        c.begun = false;
        esp_ota_test_switch_boot_to_next();
        return true;
    }

    void abort() {
        auto& c = otaUpdateCounters();
        c.abortN++;
        c.begun = false;
    }
};

inline UpdateClass Update;
