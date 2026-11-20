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
            logger.logSerialOnly(LogModule::WebServer, "SSE above limit (have=%u limit=%u) — keeping current clients\n",
                                 (unsigned)webServer.sseClientCount,
                                 (unsigned)WebSseLimits::MAX_SSE_CLIENTS);
        }
        WebServerRuntime::requestSseIncrementalBaseline(webServer);
        logger.logSerialOnly(LogModule::WebServer, "SSE connected (clients=%u, UI sessions=%u)\n",
                             (unsigned)webServer.sseClientCount,
                             (unsigned)webServer.activeUiSessionCount());
    });
    events.onDisconnect([](AsyncEventSourceClient* client) {
        (void)client;
        webServer.refreshSseClientCount();
        logger.logSerialOnly(LogModule::WebServer, "SSE off (clients=%u, UI sessions=%u, sta=%u, heap=%u, max=%u, frag=%u%%, gsm=%s)\n",
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
