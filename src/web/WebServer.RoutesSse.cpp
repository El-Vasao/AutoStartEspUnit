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
        // Cap reached: reset all so the newest browser can attach (avoids stuck zombies).
        if (webServer.sseClientCount > WebSseLimits::MAX_SSE_CLIENTS) {
            logger.logSerialOnly(LogModule::WebServer, "SSE over cap (have=%u lim=%u) — keep existing clients\n",
                                 (unsigned)webServer.sseClientCount,
                                 (unsigned)WebSseLimits::MAX_SSE_CLIENTS);
        }
        WebServerRuntime::requestSseIncrementalBaseline(webServer);
        logger.logSerialOnly(LogModule::WebServer, "SSE connected (clients=%u, uiSessions=%u)\n",
                             (unsigned)webServer.sseClientCount,
                             (unsigned)webServer.activeUiSessionCount());
    });
    events.onDisconnect([](AsyncEventSourceClient* client) {
        (void)client;
        webServer.refreshSseClientCount();
        logger.logSerialOnly(LogModule::WebServer, "SSE disconnected (clients=%u, uiSessions=%u, sta=%u, heap=%u, max=%u, frag=%u%%, gsm=%s)\n",
                             (unsigned)webServer.sseClientCount,
                             (unsigned)webServer.activeUiSessionCount(),
                             (unsigned)WiFi.softAPgetStationNum(),
                             (unsigned)espHalFreeHeap(),
                             (unsigned)espHalMaxBlock(),
                             (unsigned)0 /* heap frag N/A on ESP32 */,
                             core.getGSM().getStateString());
    });
    server.addHandler(&events);
}
