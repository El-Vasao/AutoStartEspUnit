// include/common/Logger.h
#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include "common/Constants.h"
#include "common/LogModule.h"
#include "common/Version.h"

/**
 * @brief Журнал прошивки.
 *
 * Каждая строка имеет вид `[<millis>ms] [<Модуль>] <текст>`. Модуль — обязательный первый аргумент
 * из реестра LogModule, без него вызов не скомпилируется (docs/CONVENTIONS.md, раздел «Журнал»).
 *
 * Доставка:
 * - SSE-поток `log` (`/events`): только при активной UI-сессии, подписчике и свободной очереди
 *   (решает WebServer::broadcastLog);
 * - UART: только в debug-сборке (`SERIAL_DEBUG` в генерируемом `common/Version.h`), UART0 общий с модемом.
 * История на устройстве не хранится. Прямой `Serial.print`/`printf` в прикладном коде запрещён
 * (исключение — байтовый обмен с модемом в ModemUart, это не журнал).
 *
 * Задачи: log() вызывается из основного цикла и из других задач FreeRTOS (async_tcp веб-сервера,
 * esp_timer). Строка из другой задачи кладётся в кольцо на `Logging::DEFERRED_SLOTS` мест и уходит
 * в SSE из основного цикла (flushDeferred). При переполнении строка отбрасывается, число потерь
 * сообщается следующей строкой модуля `Logger`. Из обработчика прерывания вызов игнорируется.
 */
class Logger {
public:
    Logger() = default;

    /// Запоминает задачу основного цикла. Вызывать первым в setup().
    void begin();

    /// printf-форматирование; строка оканчивается переводом строки в формате вызывающего.
    void log(LogModule module, const char* format, ...) __attribute__((format(printf, 3, 4)));

    /// Только UART при `SERIAL_DEBUG` (шумные трассировки); в SSE не уходит, в release — пустая операция.
    void logSerialOnly(LogModule module, const char* format, ...) __attribute__((format(printf, 3, 4)));

    /// Отправляет строки, отложенные другими задачами. Вызывать из основного цикла каждый проход.
    void flushDeferred();

    /// Сколько отложенных строк потеряно из-за переполнения кольца с момента старта.
    uint32_t deferredDropped() const { return _deferredDropped; }

#ifdef SERIAL_DEBUG
    bool isSerialEnabled() const { return true; }
#else
    bool isSerialEnabled() const { return false; }
#endif

private:
    uint32_t _deferredDropped = 0;
    uint32_t _deferredDroppedReported = 0;
};

/// Глобальный экземпляр (src/common/Logger.cpp).
extern Logger logger;

/**
 * Формирует строку `[<ms>ms] [<module>] <text>` в `out`. Обрезанная строка оканчивается `\n`.
 * Возвращает длину без завершающего нуля.
 */
size_t logFormatLine(char* out, size_t cap, uint32_t ms, LogModule module, const char* format, va_list args);

#ifdef UNIT_TEST
namespace logger_test {
/// true — следующие вызовы log() считаются сделанными не из основного цикла.
extern bool g_foreignTask;
} // namespace logger_test
#endif
