/**
 * @file WebServerRuntime.h
 * @brief Внутренний runtime-слой подсистемы WebServer (без публичного API).
 *
 * Назначение:
 * - Вынести “тяжёлую” логику WebServer из фасада `WebServer` в отдельный модуль,
 *   сохранив чистые границы include: внешний код видит только `include/WebServer.h`.
 *
 * Инварианты (важно для ESP8266 и памяти):
 * - Не владеет памятью и не создаёт долговременных heap-объектов.
 * - Работает только с уже существующим состоянием `WebServer` (friend access).
 * - Горячие пути избегают `String` и повторных аллокаций; для SSE/JSON используются пуловые буферы.
 *
 * Запрещено:
 * - Подключать этот заголовок из кода вне web-подсистемы (`src/web/` и подпапки).
 * - Превращать WebServerRuntime в “второй Core”: это утилитарный слой реализации.
 */
#pragma once

#include <Arduino.h>

class WebServer;
enum class CoreMode : uint8_t;

/// Internal runtime for WebServer subsystem.
/// Owns no memory; operates on WebServer state via friend access.
class WebServerRuntime {
public:
    static void update(WebServer& ws);
    static void processUiSessions(WebServer& ws);
    static void processDNS(WebServer& ws);
    static void processSseDiagnostics(WebServer& ws);

    static void start(WebServer& ws, CoreMode mode);
    static void stop(WebServer& ws);
    static void startWithParams(WebServer& ws, const char* ssid, const char* pass);

    static void broadcastLog(WebServer& ws, const char* message);
    static void broadcastStatus(WebServer& ws);
    static void broadcastStatusForce(WebServer& ws);
    /// New EventSource client: invalidate SSE dedup so next tick sends mode/gsm/hardware/clocks.
    static void requestSseIncrementalBaseline(WebServer& ws);
    static void sendStatus(WebServer& ws, size_t maxQueueDepth);
    static void tickSseIncremental(WebServer& ws, uint32_t nowMs);

    /// Stream full live JSON snapshot (bootstrap `live`, не SSE).
    static void emitLiveSnapshotJson(Print& p);

    /// UI-сессия есть, есть SSE-клиенты и avg очередь меньше порога.
    static bool sseSoftQueueAllowsSend(WebServer& ws, size_t maxAvgQueued);

    /// SSE `log`-канал: клиенты + мягкая очередь; UI-сессия не обязательна (снимает гонку с EventSource vs /ui/session).
    static bool sseLogChannelAllowsSend(WebServer& ws, size_t maxAvgQueued);

    /// Низкоуровневый доступ к SSE (инкрементальный модуль без `friend` к `events`).
    static bool sseActiveUiOk(const WebServer& ws);
    static bool sseQueueBackpressureAtLeast(WebServer& ws, size_t avgPacketsWaitingThreshold);
    static void sseSendEvent(WebServer& ws, const char* payload, const char* sseName, uint32_t ms);

    static uint16_t refreshSseClientCount(WebServer& ws);
    static bool touchUiSession(WebServer& ws, uint32_t id, uint32_t now);
    static bool closeUiSession(WebServer& ws, uint32_t id);
    static void rebuildActiveUiSessionCount(WebServer& ws);

    static bool isSameAPConfig(const WebServer& ws, const char* ssid, const char* pass);
    static void dumpAPConfig(const char* ssid, const char* pass, const char* context);
};

