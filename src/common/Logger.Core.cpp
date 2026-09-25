// src/common/Logger.Core.cpp
#include "common/Logger.h"

/**
 * @file Logger.Core.cpp
 * @brief Минимальный логгер прошивки (SSE sink).
 *
 * Назначение:
 * - Форматировать сообщения в фиксированный буфер и передавать их в текущий “транспорт” логов.
 * - В релизной прошивке логгер не должен мешать UART0 (он занят под GSM), поэтому по умолчанию
 *   используется SSE (UI) как основной канал.
 *
 * Память:
 * - Фиксированный stack-буфер `Logging::MAX_MESSAGE_LENGTH`, без heap.
 *
 * Запрещено:
 * - Делать “сырые” дампы AT в высокочастотных путях.
 * - Добавлять хранение истории логов на устройстве (это память + фрагментация).
 */

// Функция определена в WebServer.Core.cpp и рассылает логи в UI через SSE.
// Примечание: это намеренно “слабая” связность — Logger не зависит от WebServer, но вызывает глобальную функцию.
void sseBroadcastLog(const char* message);

void Logger::begin() {
#ifdef SERIAL_DEBUG
    Serial.begin(115200);
#endif
}

void Logger::log(const char* format, ...) {
    // Сообщение должно содержать префикс модуля `[...]` — политика в Logger.h.
    char buffer[Logging::MAX_MESSAGE_LENGTH];
    int prefixLen = snprintf(buffer, sizeof(buffer), "[%lums] ", (unsigned long)millis());
    if (prefixLen < 0) {
        prefixLen = 0;
    }
    if ((size_t)prefixLen >= sizeof(buffer)) {
        prefixLen = (int)(sizeof(buffer) - 1);
    }
    va_list args;
    va_start(args, format);
    vsnprintf(buffer + (size_t)prefixLen, sizeof(buffer) - (size_t)prefixLen, format, args);
    va_end(args);

    // Отправляем сообщение всем подключённым SSE-клиентам.
    // Важно: логгер не буферизует историю; UI хранит её на стороне клиента.
    sseBroadcastLog(buffer);
#ifdef SERIAL_DEBUG
    Serial.print(buffer);
#endif
}

void Logger::logSerialOnly(const char* format, ...) {
    va_list args;
    va_start(args, format);
#ifdef SERIAL_DEBUG
    char buffer[Logging::MAX_MESSAGE_LENGTH];
    int prefixLen = snprintf(buffer, sizeof(buffer), "[%lums] ", (unsigned long)millis());
    if (prefixLen < 0) {
        prefixLen = 0;
    }
    if ((size_t)prefixLen >= sizeof(buffer)) {
        prefixLen = (int)(sizeof(buffer) - 1);
    }
    vsnprintf(buffer + (size_t)prefixLen, sizeof(buffer) - (size_t)prefixLen, format, args);
    va_end(args);
    Serial.print(buffer);
#else
    va_end(args);
    (void)format;
#endif
}

Logger logger;

