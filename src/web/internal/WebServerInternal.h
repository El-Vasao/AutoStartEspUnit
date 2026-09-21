/**
 * @file WebServerInternal.h
 * @brief Внутренние утилиты WebServer (JSON ответы, заголовки кэша, парсинг параметров).
 *
 * Важно:
 * - Этот файл — часть реализации web-подсистемы. Его нельзя включать вне `src/web/` (и подпапок).
 * - JSON body: один emit в буфер (`sendJsonBuffered`), без double-pass SkippingPrint.
 *
 * Запрещено:
 * - Добавлять сюда “общепроектные” зависимости: только то, что нужно WebServer.
 * - Возвращать `String` из hot-path helpers (делаем работу на `char[]` и `snprintf`).
 */
#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <stdlib.h>

#include "web/WebServer.h"
#include "fs/FSManager.h"
#include "core/Core.h"
#include "common/Logger.h"
#include "common/Constants.h"

namespace web_internal {

static constexpr const char* kContentTypeJson = "application/json";
static constexpr const char* kContentTypeText = "text/plain";
static constexpr size_t kJsonBufferedMaxBytes = JsonBytes::Web::BOOTSTRAP_JSON_MAX;

using JsonEmitFn = void (*)(Print&);

/** Print that writes into a fixed heap/stack buffer; overflow sets a flag. */
class BufferingPrint final : public Print {
public:
    BufferingPrint(uint8_t* out, size_t cap) : out_(out), cap_(cap), len_(0), overflow_(false) {}

    size_t length() const { return len_; }
    bool overflowed() const { return overflow_; }

    size_t write(uint8_t b) override {
        if (len_ >= cap_) {
            overflow_ = true;
            return 1;
        }
        out_[len_++] = b;
        return 1;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        if (!buffer || size == 0) return 0;
        for (size_t i = 0; i < size; ++i) {
            write(buffer[i]);
        }
        return size;
    }

private:
    uint8_t* out_;
    size_t cap_;
    size_t len_;
    bool overflow_;
};

static inline void addNoCacheHeaders(AsyncWebServerResponse* resp) {
    if (!resp) return;
    resp->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    resp->addHeader("Pragma", "no-cache");
    resp->addHeader("Expires", "0");
}

static inline void addSessionRevalidateHeaders(AsyncWebServerResponse* resp, const char* etag) {
    if (!resp) return;
    resp->addHeader("Cache-Control", WebCache::SESSION_REVALIDATE);
    if (etag && etag[0]) {
        resp->addHeader("ETag", etag);
    }
}

static inline bool urlEndsWith(const char* url, const char* suffix) {
    if (!url || !suffix) return false;
    size_t lu = strlen(url);
    size_t ls = strlen(suffix);
    return lu >= ls && strcmp(url + (lu - ls), suffix) == 0;
}

static inline const char* contentTypeByPath(const char* urlPath) {
    if (!urlPath) return "application/octet-stream";
    if (urlEndsWith(urlPath, ".js")) return "application/javascript";
    if (urlEndsWith(urlPath, ".css")) return "text/css";
    if (urlEndsWith(urlPath, ".html")) return "text/html; charset=utf-8";
    if (urlEndsWith(urlPath, ".ico")) return "image/x-icon";
    if (urlEndsWith(urlPath, ".svg")) return "image/svg+xml";
    return "application/octet-stream";
}

static inline bool isCaptiveProbeUrl(const char* urlPath) {
    if (!urlPath) return false;
    return strcmp(urlPath, "/generate_204") == 0 ||
           strcmp(urlPath, "/fwlink") == 0 ||
           strcmp(urlPath, "/connecttest.txt") == 0 ||
           strcmp(urlPath, "/hotspot-detect.html") == 0 ||
           strcmp(urlPath, "/library/test/success.html") == 0 ||
           strcmp(urlPath, "/kindle-wifi/wifistub.html") == 0;
}

static inline bool parseUint32Param(AsyncWebServerRequest* request, const char* name, uint32_t* out) {
    if (!request || !name || !out || !request->hasParam(name, true)) return false;
    const String& value = request->getParam(name, true)->value();
    if (value.length() == 0) return false;
    char* end = nullptr;
    const unsigned long parsed = strtoul(value.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    *out = static_cast<uint32_t>(parsed);
    return true;
}

// JSON by contract is always served without gzip.
static inline bool sendJsonFromFs(AsyncWebServerRequest* request, const char* path,
                                  const char* cacheControl) {
    if (!path || path[0] != '/') {
        request->send(500, kContentTypeText, "Bad path");
        return false;
    }
    if (!fileSystem.exists(path)) {
        request->send(404, kContentTypeJson, "{\"success\":false,\"error\":\"FILE_NOT_FOUND\"}");
        return false;
    }
    AsyncWebServerResponse* resp = request->beginResponse(fileSystem.webFs(), path, kContentTypeJson);
    resp->addHeader("Cache-Control", cacheControl ? cacheControl : "no-cache");
    resp->addHeader("Pragma", "no-cache");
    resp->addHeader("Expires", "0");
    request->send(resp);
    return true;
}

/**
 * Emit JSON once into a heap buffer, then send (AsyncWebServer copies into response).
 * Cap keeps SoftAP responses bounded; raise if a new endpoint needs a larger body.
 */
static inline bool sendJsonBuffered(AsyncWebServerRequest* request, JsonEmitFn emit,
                                    size_t maxBytes = kJsonBufferedMaxBytes) {
    if (!request || !emit || maxBytes == 0) {
        if (request) request->send(500, kContentTypeText, "Bad JSON emit");
        return false;
    }
    uint8_t* buf = static_cast<uint8_t*>(malloc(maxBytes + 1));
    if (!buf) {
        request->send(503, kContentTypeJson, "{\"success\":false,\"error\":\"OUT_OF_MEMORY\"}");
        return false;
    }
    BufferingPrint bp(buf, maxBytes);
    emit(bp);
    if (bp.overflowed() || bp.length() == 0) {
        free(buf);
        request->send(500, kContentTypeText, bp.overflowed() ? "JSON too large" : "Empty JSON");
        return false;
    }
    buf[bp.length()] = '\0';
    AsyncWebServerResponse* resp = request->beginResponse(200, kContentTypeJson, reinterpret_cast<const char*>(buf));
    free(buf);
    if (!resp) {
        request->send(503, kContentTypeJson, "{\"success\":false,\"error\":\"RESPONSE_ALLOC_FAILED\"}");
        return false;
    }
    addNoCacheHeaders(resp);
    request->send(resp);
    return true;
}

/** Alias kept for older call sites. */
static inline bool sendJsonResponse(AsyncWebServerRequest* request, JsonEmitFn emit) {
    return sendJsonBuffered(request, emit);
}

static inline void sendBusy(AsyncWebServerRequest* request) {
    AsyncWebServerResponse* resp = request->beginResponse(
        409, kContentTypeJson,
        "{\"success\":false,\"error\":\"BUSY\",\"message\":\"Flash operation in progress\"}"
    );
    request->send(resp);
}

static inline bool rejectIfFlashBusy(AsyncWebServerRequest* request) {
    if (!webServer.isFlashBusy()) return false;
    sendBusy(request);
    return true;
}

static inline void sendJsonSuccess(AsyncWebServerRequest* request) {
    AsyncWebServerResponse* resp = request->beginResponse(200, kContentTypeJson, "{\"success\":true}");
    if (!resp) {
        request->send(503, kContentTypeJson, "{\"success\":false,\"error\":\"RESPONSE_ALLOC_FAILED\"}");
        return;
    }
    request->send(resp);
}

} // namespace web_internal
