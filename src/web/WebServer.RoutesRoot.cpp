#include "web/WebServer.h"
#include "web/internal/WebServerInternal.h"
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

        if (fileSystem.hasRequiredWebAssets()) {
            const char* etag = core.getVersionString();
            auto addHeaders = [&](AsyncWebServerResponse* response) {
                addSessionRevalidateHeaders(response, etag);
                response->addHeader("Content-Security-Policy",
                    "default-src 'self'; "
                    "script-src 'self' 'unsafe-eval' 'unsafe-inline'; "
                    "style-src 'self' 'unsafe-inline';");
            };

            if (fileSystem.exists("/index.html.gz")) {
                AsyncWebServerResponse* response =
                    request->beginResponse(fileSystem.webFs(), "/index.html.gz", "text/html; charset=utf-8");
                response->addHeader("Content-Encoding", "gzip");
                response->addHeader("Vary", "Accept-Encoding");
                addHeaders(response);
                request->send(response);
                return;
            }
            sendFallback();
        } else {
            sendFallback();
        }
    });
}

