// include/common/Logger.h
#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include "common/Constants.h"
#include "common/Version.h"

/**
 * @brief Централизованная система логирования.
 *
 * Весь диагностический вывод прошивки должен идти через `logger.log()` — тогда сообщения
 * попадают в веб-поток `log` (SSE `/events`).
 * Прямой `Serial.print` / `printf` в коде приложения не использовать (исключение: низкоуровневый
 * байтовый обмен с модемом в `ModemUart`/`HardwareSerial` — это не «лог», на SSE не дублировать).
 * Симметрия транспортов:
 * — `debug`-сборка (`SERIAL_DEBUG` в генерируемом `common/Version.h`): каждая строка `logger.log`
 *   уходит в SSE (если есть подписчик и очередь не перегружена) и зеркится в UART;
 * — `release`: только SSE при активной UI-сессии и подписчике `/events`, без UART.
 * — `logSerialOnly`: только UART в debug-сборке; в release вызовы игнорируются (без SSE, без форматирования в проде).
 * Лог в SSE gated: нужна активная UI-сессия, свободный heap/maxBlock и очередь ниже
 * `WebSseLimits::SSE_SOFT_QUEUE_MAX` / hard `SSE_MAX_QUEUED_MESSAGES` (см. platformio.ini).
 *
 * Не вызывать `sseBroadcastLog` напрямую из прикладного кода.
 *
 * Префикс модуля (обязательно): каждая диагностическая строка должна явно указывать источник —
 * в начале сообщения фиксированный тег вида `[ИмяМодуля]` (как в существующих логах: `[WebServer]`,
 * `[FSManager]`, `[MQTT]` и т.д.). Без префикса или с неоднозначным источником лог писать нельзя.
 *
 * Дизайн:
 * - история на устройстве не хранится (нет флеш-логов), только поток в SSE (UI);
 * - форматирование в фиксированный буфер без динамических аллокаций в `log()`;
 * - каждая строка начинается с `[Nms]` (`millis()`), затем префикс модуля.
 */
class Logger {
public:
    Logger() = default;

    /**
     * @brief Инициализация логгера.
     */
    void begin();

    /**
     * @brief Записать форматированное сообщение.
     *
     * Как printf. Первый фрагмент формата (или строки) — префикс модуля в квадратных скобках,
     * см. правило в заголовке класса. Доставка: SSE при активной UI-сессии (см. заголовок класса).
     *
     * @param format Строка форматирования.
     * @param ... Аргументы.
     */
    void log(const char* format, ...) __attribute__((format(printf, 2, 3)));

    /// Только UART при `SERIAL_DEBUG`; на SSE не уходит (шумные/частые трассировки).
    void logSerialOnly(const char* format, ...) __attribute__((format(printf, 2, 3)));

#ifdef SERIAL_DEBUG
    bool isSerialEnabled() const { return true; }
#else
    bool isSerialEnabled() const { return false; }
#endif

private:
};

/// Глобальный экземпляр логгера (определён в Logger.cpp)
extern Logger logger;

