// src/common/Logger.cpp
#include "common/Logger.h"

#include <stdio.h>
#include <string.h>

#if !defined(UNIT_TEST)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

// Defined in WebServer.Core.cpp (tests provide their own sink). Called only from the main loop task.
void sseBroadcastLog(const char* message);

namespace {

#if defined(UNIT_TEST)
bool isrContext() { return false; }
bool onLoopTask() { return !logger_test::g_foreignTask; }
void rememberLoopTask() {}
struct RingLock {};
#else
TaskHandle_t s_loopTask = nullptr;
portMUX_TYPE s_ringMux = portMUX_INITIALIZER_UNLOCKED;

bool isrContext() { return xPortInIsrContext(); }
// Before begin() everything runs in setup() on the loop task.
bool onLoopTask() { return s_loopTask == nullptr || xTaskGetCurrentTaskHandle() == s_loopTask; }
void rememberLoopTask() { s_loopTask = xTaskGetCurrentTaskHandle(); }
struct RingLock {
    RingLock() { taskENTER_CRITICAL(&s_ringMux); }
    ~RingLock() { taskEXIT_CRITICAL(&s_ringMux); }
};
#endif

char s_ring[Logging::DEFERRED_SLOTS][Logging::MAX_MESSAGE_LENGTH];
size_t s_ringHead = 0;   // next slot to read
size_t s_ringCount = 0;

void emit(const char* line) {
    sseBroadcastLog(line);
#if defined(SERIAL_DEBUG) && !defined(UNIT_TEST)
    Serial.print(line);
#endif
}

} // namespace

#if defined(UNIT_TEST)
namespace logger_test {
bool g_foreignTask = false;
} // namespace logger_test
#endif

size_t logFormatLine(char* out, size_t cap, uint32_t ms, LogModule module, const char* format, va_list args) {
    if (!out || cap == 0) return 0;
    int n = snprintf(out, cap, "[%lums] [%s] ", (unsigned long)ms, logModuleName(module));
    if (n < 0) n = 0;
    size_t len = (size_t)n < cap ? (size_t)n : cap - 1;
    if (format && len < cap - 1) {
        const int m = vsnprintf(out + len, cap - len, format, args);
        if (m > 0) len += (size_t)m;
    }
    if (len >= cap - 1) {
        len = cap - 1;
        if (cap >= 2) out[cap - 2] = '\n';
        out[cap - 1] = '\0';
    }
    return len;
}

void Logger::begin() {
#if defined(SERIAL_DEBUG) && !defined(UNIT_TEST)
    Serial.begin(Gsm::UART_BAUD);
#endif
    rememberLoopTask();
}

void Logger::log(LogModule module, const char* format, ...) {
    if (isrContext()) return;
    char buffer[Logging::MAX_MESSAGE_LENGTH];
    va_list args;
    va_start(args, format);
    logFormatLine(buffer, sizeof(buffer), (uint32_t)millis(), module, format, args);
    va_end(args);

    if (onLoopTask()) {
        flushDeferred();
        emit(buffer);
        return;
    }

    RingLock lock;
    if (s_ringCount >= Logging::DEFERRED_SLOTS) {
        _deferredDropped++;
        return;
    }
    const size_t slot = (s_ringHead + s_ringCount) % Logging::DEFERRED_SLOTS;
    memcpy(s_ring[slot], buffer, sizeof(buffer));
    s_ringCount++;
}

void Logger::flushDeferred() {
    if (!onLoopTask()) return;
    char line[Logging::MAX_MESSAGE_LENGTH];
    for (;;) {
        bool have = false;
        uint32_t dropped = 0;
        {
            RingLock lock;
            if (s_ringCount != 0) {
                memcpy(line, s_ring[s_ringHead], sizeof(line));
                s_ringHead = (s_ringHead + 1) % Logging::DEFERRED_SLOTS;
                s_ringCount--;
                have = true;
            } else {
                dropped = _deferredDropped - _deferredDroppedReported;
                _deferredDroppedReported = _deferredDropped;
            }
        }
        if (have) {
            emit(line);
            continue;
        }
        if (dropped != 0) {
            snprintf(line, sizeof(line), "[%lums] [%s] %u deferred lines dropped (ring full)\n",
                     (unsigned long)millis(), logModuleName(LogModule::Logger), (unsigned)dropped);
            emit(line);
        }
        return;
    }
}

void Logger::logSerialOnly(LogModule module, const char* format, ...) {
#if defined(SERIAL_DEBUG) && !defined(UNIT_TEST)
    char buffer[Logging::MAX_MESSAGE_LENGTH];
    va_list args;
    va_start(args, format);
    logFormatLine(buffer, sizeof(buffer), (uint32_t)millis(), module, format, args);
    va_end(args);
    Serial.print(buffer);
#else
    (void)module;
    (void)format;
#endif
}

Logger logger;
