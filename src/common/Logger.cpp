// src/common/Logger.cpp
#include "common/Logger.h"

#include <stdio.h>
#include <string.h>

#include "common/Pins.h"

#if !defined(UNIT_TEST)
#include "common/Version.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_rom_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/uart_periph.h"
#endif

// Определена в WebServer.Core.cpp (тесты подставляют свой приёмник). Вызывается только из задачи основного цикла.
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
bool s_logUartReady = false;

// RX обязателен для uart_set_pin на C3; GPIO6 в проекте не занят.
constexpr int kLogRxDummy = 6;
constexpr uart_port_t kLogUart = UART_NUM_1;

bool isrContext() { return xPortInIsrContext(); }
bool onLoopTask() { return s_loopTask == nullptr || xTaskGetCurrentTaskHandle() == s_loopTask; }
void rememberLoopTask() { s_loopTask = xTaskGetCurrentTaskHandle(); }
struct RingLock {
    RingLock() { taskENTER_CRITICAL(&s_ringMux); }
    ~RingLock() { taskEXIT_CRITICAL(&s_ringMux); }
};
#endif

char s_ring[Logging::DEFERRED_SLOTS][Logging::MAX_MESSAGE_LENGTH];
size_t s_ringHead = 0;
size_t s_ringCount = 0;

void uartPrint(const char* line) {
#if !defined(UNIT_TEST)
    if (!s_logUartReady || !line) return;
    const size_t n = strlen(line);
    if (n) (void)uart_write_bytes(kLogUart, line, n);
#else
    (void)line;
#endif
}

#if !defined(UNIT_TEST)
void probeLogPad_() {
    // Медленные фронты на Pin::LOG_TX: мультиметр/светодиод на COM5-RX должны видеть ~5 Гц.
    gpio_reset_pin(static_cast<gpio_num_t>(Pin::LOG_TX));
    gpio_set_direction(static_cast<gpio_num_t>(Pin::LOG_TX), GPIO_MODE_OUTPUT);
    for (int i = 0; i < 10; i++) {
        gpio_set_level(static_cast<gpio_num_t>(Pin::LOG_TX), i & 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

bool ensureLogUart_(bool force = false) {
    if (s_logUartReady && !force) return true;

    Serial1.end();
    if (uart_is_driver_installed(kLogUart)) {
        (void)uart_driver_delete(kLogUart);
    }
    s_logUartReady = false;

    probeLogPad_();

    uart_config_t cfg{};
    cfg.baud_rate = static_cast<int>(Logging::UART_BAUD);
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_XTAL;
    cfg.rx_flow_ctrl_thresh = 0;

    // Как в Arduino HAL: install → param → pins. RX buf > FIFO (128).
    esp_err_t err = uart_driver_install(kLogUart, 256, 256, 0, nullptr, 0);
    if (err == ESP_OK) err = uart_param_config(kLogUart, &cfg);
    if (err == ESP_OK) {
        err = uart_set_pin(kLogUart, Pin::LOG_TX, kLogRxDummy,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        esp_rom_gpio_connect_out_signal(
            Pin::LOG_TX,
            UART_PERIPH_SIGNAL(kLogUart, SOC_UART_TX_PIN_IDX),
            false, false);
    }

    s_logUartReady = (err == ESP_OK);
    if (!s_logUartReady) {
        // Быстрое мигание = UART init failed (пайка/пин живы, драйвер нет).
        for (int i = 0; i < 40; i++) {
            gpio_set_level(static_cast<gpio_num_t>(Pin::LOG_TX), i & 1);
            vTaskDelay(pdMS_TO_TICKS(25));
        }
        return false;
    }

    char banner[96];
    snprintf(banner, sizeof(banner),
             "\n[log] UART1 TX GPIO%u %lu 8N1 fw %s\n",
             (unsigned)Pin::LOG_TX, (unsigned long)Logging::UART_BAUD, FIRMWARE_VERSION_STR);
    uartPrint(banner);
    (void)uart_wait_tx_done(kLogUart, pdMS_TO_TICKS(100));
    return true;
}
#endif

void emit(const char* line) {
    sseBroadcastLog(line);
    uartPrint(line);
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
#if !defined(UNIT_TEST)
    (void)ensureLogUart_(/*force=*/true);
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
#if !defined(UNIT_TEST)
    char buffer[Logging::MAX_MESSAGE_LENGTH];
    va_list args;
    va_start(args, format);
    logFormatLine(buffer, sizeof(buffer), (uint32_t)millis(), module, format, args);
    va_end(args);
    uartPrint(buffer);
#else
    (void)module;
    (void)format;
#endif
}

void Logger::flushUart() {
#if !defined(UNIT_TEST)
    if (s_logUartReady) {
        (void)uart_wait_tx_done(kLogUart, pdMS_TO_TICKS(200));
    }
#endif
}

Logger logger;

#if !defined(UNIT_TEST)
extern "C" void initVariant() {
    if (!ensureLogUart_()) return;
    logger.log(LogModule::Logger, "early init (before setup)\n");
}
#endif
