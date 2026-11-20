#include "web/WebServer.h"
#include "web/internal/WebServerInternal.h"
#include "web/internal/WebRamAssets.h"

using namespace web_internal;

namespace {

bool sendRam(AsyncWebServerRequest* request, const char* path, const char* contentType, bool gzip,
             bool sessionCache) {
    AsyncWebServerResponse* resp = nullptr;
    const web_ram::Take take = web_ram::acquire(request, path, contentType, gzip, &resp);
    if (take == web_ram::Take::Missing) return false;
    if (take != web_ram::Take::Ready || !resp) {
        request->send(503, "text/plain", "Not Ready");
        return true;
    }
    if (sessionCache) addSessionRevalidateHeaders(resp, core.getVersionString());
    else addNoCacheHeaders(resp);
    request->send(resp);
    return true;
}

} // namespace

void WebServer::setupCaptivePortalRoutes_() {
    server.onNotFound([](AsyncWebServerRequest* request) {
        char urlBuf[BufferBytes::Web::URL];
        const String& u = request->url();
        strlcpy(urlBuf, u.c_str(), sizeof(urlBuf));

        if (request->method() == HTTP_GET && isCaptiveProbeUrl(urlBuf)) {
            request->redirect("/");
            return;
        }

        size_t urlLen = strlen(urlBuf);
        if (urlLen > 1 && urlBuf[urlLen - 1] != '/') {
            const char* contentType = contentTypeByPath(urlBuf);

            if (urlEndsWith(urlBuf, ".json")) {
                // Схемы и index программ: RAM или чтение на цикле, не LittleFS в колбэке.
                AsyncWebServerResponse* resp = nullptr;
                const web_ram::Take take = web_ram::acquire(request, urlBuf, kContentTypeJson, false, &resp);
                if (take == web_ram::Take::Missing) {
                    request->send(404, kContentTypeJson, "{\"success\":false,\"error\":\"FILE_NOT_FOUND\"}");
                    return;
                }
                if (take != web_ram::Take::Ready || !resp) {
                    request->send(503, kContentTypeJson, "{\"success\":false,\"error\":\"NOT_READY\"}");
                    return;
                }
                addNoCacheHeaders(resp);
                request->send(resp);
                return;
            }

            // На ФС статика только как <path>.gz. Отдача — копия в RAM или окно цикла.
            if (urlEndsWith(urlBuf, ".js") || urlEndsWith(urlBuf, ".css") || urlEndsWith(urlBuf, ".html") ||
                urlEndsWith(urlBuf, ".ico")) {
                char gzPath[BufferBytes::Fs::GZIP_PATH];
                const int n = snprintf(gzPath, sizeof(gzPath), "%s.gz", urlBuf);
                if (n > 0 && (size_t)n < sizeof(gzPath) &&
                    sendRam(request, gzPath, contentType, true, true)) {
                    return;
                }
                if (urlEndsWith(urlBuf, ".js") || urlEndsWith(urlBuf, ".css")) {
                    request->send(404, "text/plain", "Not Found");
                    return;
                }
            }
        }

        if (request->method() == HTTP_GET &&
            (urlEndsWith(urlBuf, ".js") || urlEndsWith(urlBuf, ".css"))) {
            request->send(404, "text/plain", "Not Found");
            return;
        }

        request->redirect("/");
    });
}
