#include "web/WebServer.h"
#include "common/EspHal.h"
#include "web/internal/WebServerInternal.h"
#include "web/internal/WebServerRuntime.h"
#include "common/Logger.h"
#include "core/Core.h"
#include "gsm/GsmController.h"
#include <WiFi.h>

using namespace web_internal;

void WebServer::setupSseEndpointRoutes_() {
    events.onConnect([](AsyncEventSourceClient* client) {
        (void)client;
        webServer.refreshSseClientCount();
        // Потолок достигнут: сбросить всех, чтобы самый новый браузер мог подключиться (чтобы не залипали зомби).
        if (webServer.sseClientCount > WebSseLimits::MAX_SSE_CLIENTS) {
            logger.logSerialOnly(LogModule::WebServer, "SSE выше предела (есть=%u предел=%u) — оставляю текущих клиентов\n",
                                 (unsigned)webServer.sseClientCount,
                                 (unsigned)WebSseLimits::MAX_SSE_CLIENTS);
        }
        WebServerRuntime::requestSseIncrementalBaseline(webServer);
        logger.logSerialOnly(LogModule::WebServer, "SSE подключён (клиенты=%u, сессии UI=%u)\n",
                             (unsigned)webServer.sseClientCount,
                             (unsigned)webServer.activeUiSessionCount());
    });
    events.onDisconnect([](AsyncEventSourceClient* client) {
        (void)client;
        webServer.refreshSseClientCount();
        logger.logSerialOnly(LogModule::WebServer, "SSE отключён (клиенты=%u, сессии UI=%u, sta=%u, куча=%u, макс=%u, фраг=%u%%, gsm=%s)\n",
                             (unsigned)webServer.sseClientCount,
                             (unsigned)webServer.activeUiSessionCount(),
                             (unsigned)WiFi.softAPgetStationNum(),
                             (unsigned)espHalFreeHeap(),
                             (unsigned)espHalMaxBlock(),
                             (unsigned)0 /* фрагментация кучи неприменима на ESP32 */,
                             core.getGSM().getStateString());
    });
    server.addHandler(&events);
}
