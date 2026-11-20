#include "web/WebServer.h"
#include "web/internal/WebServerInternal.h"
#include "web/internal/WebRamAssets.h"
#include "web/FallbackWebPage.h"

using namespace web_internal;

void WebServer::setupRootRoutes_() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        auto sendFallback = [&]() {
            AsyncWebServerResponse* response = request->beginResponse(
                200,
                "text/html; charset=utf-8",
                reinterpret_cast<const uint8_t*>(FALLBACK_INDEX_HTML),
                sizeof(FALLBACK_INDEX_HTML) - 1
            );
            addNoCacheHeaders(response);
            response->addHeader("Content-Security-Policy",
                                "default-src 'self'; "
                                "script-src 'self' 'unsafe-inline'; "
                                "style-src 'self' 'unsafe-inline';");
            request->send(response);
        };

        // index.html.gz уже в RAM (или окно с цикла). LittleFS и beginResponse(FS) отсюда нельзя.
        AsyncWebServerResponse* response = nullptr;
        const web_ram::Take take =
            web_ram::acquire(request, "/index.html.gz", "text/html; charset=utf-8", true, &response);
        if (take == web_ram::Take::Missing || !response) {
            if (take == web_ram::Take::Unavailable) {
                request->send(503, "text/plain", "Not Ready");
                return;
            }
            sendFallback();
            return;
        }
        const char* etag = core.getVersionString();
        addSessionRevalidateHeaders(response, etag);
        response->addHeader("Content-Security-Policy",
            "default-src 'self'; "
            "script-src 'self' 'unsafe-eval' 'unsafe-inline'; "
            "style-src 'self' 'unsafe-inline';");
        request->send(response);
    });
}
