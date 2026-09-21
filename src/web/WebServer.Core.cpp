/**
 * @file WebServer.Core.cpp
 * @brief Тонкий фасад класса `WebServer` (делегирование в runtime-слой).
 *
 * Принципы:
 * - Этот файл должен оставаться “лёгким”: без тяжёлой логики и без горячих циклов.
 * - Основная логика AP/DNS/UI sessions/SSE находится в `WebServerRuntime` (см. `src/web/WebServer.Runtime.cpp`).
 *
 * Память:
 * - Мы избегаем дублирования данных: всё состояние хранится в `WebServer`, runtime только работает с ним.
 *
 * Запрещено:
 * - Переносить сюда реализацию runtime/статуса/маршрутов — только фасад и glue-код.
 */
#include "web/WebServer.h"
#include "web/internal/WebServerRuntime.h"
#include "core/FlashCommitCoordinator.h"
#include "core/Core.h"
#include <DNSServer.h>
#include <WiFi.h>
#include "common/Logger.h"
#include "common/Constants.h"

namespace {

bool sseMutedDuringBoot() {
    return core.getMode() == CoreMode::BOOT;
}

} // namespace

WebServer webServer;

const byte DNS_PORT = WebConfig::DNS_PORT;
DNSServer dnsServer;
IPAddress apIP;

WebServer::WebServer()
    : sseClientCount(0),
      activeUiSessionCount_(0),
      lastSseDiagMs_(0),
      lastObservedSseClients_(0),
      sseDiagTailUntilMs_(0),
      lastUiActivityMs_(0),
      server(WebConfig::HTTP_PORT),
      apActive(false),
      serverActive(false),
      routesConfigured(false),
      lastApSsid{},
      lastApPass{},
      apConfigKnown(false),
      postedConfigJsonPending(false),
      postedProgramJsonPending(false),
      events("/events") {}

bool WebServer::isFlashBusy() const {
    // Важно: здесь учитываем и “глобальные” deferred FS операции, и локальные POST-потоки web.
    // Это общий предохранитель от пересечения операций с flash/LittleFS.
    return flashCommit.isPending() || postedConfigJsonPending || postedProgramJsonPending;
}

void WebServer::update() {
    WebServerRuntime::update(*this);
}

void WebServer::processUiSessions() {
    WebServerRuntime::processUiSessions(*this);
}

void WebServer::processDNS() {
    WebServerRuntime::processDNS(*this);
}

void WebServer::processSseDiagnostics() {
    WebServerRuntime::processSseDiagnostics(*this);
}

void WebServer::start(CoreMode mode) {
    WebServerRuntime::start(*this, mode);
}

void WebServer::stop() {
    WebServerRuntime::stop(*this);
}

bool WebServer::isActive() const {
    return apActive && serverActive;
}

IPAddress WebServer::getAPIP() const {
    return WiFi.softAPIP();
}

void WebServer::startWithParams(const char* ssid, const char* pass) {
    WebServerRuntime::startWithParams(*this, ssid, pass);
}

// Routes are implemented in `src/web/*` after split.
void WebServer::setupRoutes() {
    setupSseEndpointRoutes_();
    setupRootRoutes_();
    setupApiRoutes_();

    // ---------- СТАТИЧЕСКИЕ ФАЙЛЫ (после API, до onNotFound) ----------
    // Axiom: UI assets on LittleFS are only *.gz; onNotFound maps URL → URL.gz.
    setupCaptivePortalRoutes_();
}

bool WebServer::isSameAPConfig(const char* ssid, const char* pass) const {
    return WebServerRuntime::isSameAPConfig(*this, ssid, pass);
}

void WebServer::dumpAPConfig(const char* ssid, const char* pass, const char* context) const {
    WebServerRuntime::dumpAPConfig(ssid, pass, context);
}

uint16_t WebServer::refreshSseClientCount() {
    return WebServerRuntime::refreshSseClientCount(*this);
}

bool WebServer::touchUiSession(uint32_t id, uint32_t now) {
    return WebServerRuntime::touchUiSession(*this, id, now);
}

bool WebServer::closeUiSession(uint32_t id) {
    return WebServerRuntime::closeUiSession(*this, id);
}

void WebServer::rebuildActiveUiSessionCount() {
    WebServerRuntime::rebuildActiveUiSessionCount(*this);
}

void WebServer::broadcastLog(const char* message) {
    if (sseMutedDuringBoot()) return;
    WebServerRuntime::broadcastLog(*this, message);
}

void WebServer::tickSseIncremental(uint32_t nowMs) {
    if (sseMutedDuringBoot()) return;
    WebServerRuntime::tickSseIncremental(*this, nowMs);
}

void WebServer::broadcastStatus() {
    if (sseMutedDuringBoot()) return;
    WebServerRuntime::broadcastStatus(*this);
}


void WebServer::broadcastStatusForce() {
    if (sseMutedDuringBoot()) return;
    WebServerRuntime::broadcastStatusForce(*this);
}

void WebServer::sendStatus_(size_t maxQueueDepth) {
    if (sseMutedDuringBoot()) return;
    WebServerRuntime::sendStatus(*this, maxQueueDepth);
}

void sseBroadcastLog(const char* message) {
    webServer.broadcastLog(message);
}

void sseBroadcastStatus() {
    webServer.tickSseIncremental(millis());
}

