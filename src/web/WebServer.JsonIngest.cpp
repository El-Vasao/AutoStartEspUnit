// src/web/WebServer.JsonIngest.cpp
#include "web/WebServer.h"
#include "common/EspHal.h"
#include "fs/FsManager.h"
#include "core/FlashCommitCoordinator.h"
#include "common/Logger.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

namespace {
static constexpr const char* kContentTypeJson = "application/json";

static void sendBusy(AsyncWebServerRequest* request) {
    AsyncWebServerResponse* resp = request->beginResponse(409, kContentTypeJson,
        "{\"success\":false,\"error\":\"BUSY\",\"message\":\"Flash operation in progress\"}");
    request->send(resp);
}

static void sendJsonSuccess(AsyncWebServerRequest* request) {
    AsyncWebServerResponse* resp = request->beginResponse(200, kContentTypeJson, "{\"success\":true}");
    request->send(resp);
}

/// Состояние потоковой записи POST JSON во временный файл (тело догоняется в handleBody; финал — в onRequest).
struct RawJsonPostCtx {
    File f;
    bool failed{false};
    bool busyRejected{false};
    bool oversize{false};
    bool ioError{false};
    bool heldPending{false};
};

static constexpr uintptr_t kTempAllocFailedTag = 0x1u;

static RawJsonPostCtx gRawJsonPostPool[2];
static bool gRawJsonPostUsed[2]{false, false};

static RawJsonPostCtx* acquireRawJsonCtx() {
    // Однопоточно: простой фиксированный пул.
    for (int i = 0; i < 2; i++) {
        if (!gRawJsonPostUsed[i]) {
            gRawJsonPostUsed[i] = true;
            gRawJsonPostPool[i] = RawJsonPostCtx{};
            return &gRawJsonPostPool[i];
        }
    }
    return nullptr;
}

static void releaseRawJsonCtx(RawJsonPostCtx* ctx) {
    if (!ctx) return;
    for (int i = 0; i < 2; i++) {
        if (ctx == &gRawJsonPostPool[i]) {
            gRawJsonPostUsed[i] = false;
            return;
        }
    }
}

static bool ensureHttpTmpDir() {
    return fileSystem.ensurePath(HttpPostJson::TMP_CONFIG);
}
} // namespace

const char* WebServer::jsonPostTmpPath(JsonPostStreamKind k) {
    return (k == JsonPostStreamKind::Config) ? HttpPostJson::TMP_CONFIG : HttpPostJson::TMP_PROGRAM;
}

bool WebServer::jsonPostOtherSidePending(JsonPostStreamKind k) {
    return (k == JsonPostStreamKind::Config) ? webServer.postedProgramJsonPending
                                             : webServer.postedConfigJsonPending;
}

void WebServer::jsonPostSetThisPending(JsonPostStreamKind k, bool v) {
    if (k == JsonPostStreamKind::Config) webServer.postedConfigJsonPending = v;
    else webServer.postedProgramJsonPending = v;
}

void WebServer::jsonPostSetDeferred(JsonPostStreamKind k) {
    if (k == JsonPostStreamKind::Config) flashCommit.deferPostedConfigApply();
    else flashCommit.deferPostedProgramApply();
}

void WebServer::jsonPostStreamOnBody(JsonPostStreamKind k, AsyncWebServerRequest* request, uint8_t* data,
                                     size_t len, size_t index, size_t total) {
    if (index == 0) {
        if (jsonPostOtherSidePending(k)) {
            logger.log(LogModule::WebServer, "%s: rejected, other JSON stream active\n",
                       k == JsonPostStreamKind::Config ? "config" : "program");
            auto* c = acquireRawJsonCtx();
            if (!c) {
                request->_tempObject = reinterpret_cast<void*>(kTempAllocFailedTag);
                return;
            }
            c->busyRejected = true;
            request->_tempObject = c;
            return;
        }
        if (webServer.isFlashBusy()) {
            logger.log(LogModule::WebServer, "%s: rejected, isFlashBusy\n",
                       k == JsonPostStreamKind::Config ? "config" : "program");
            auto* c = acquireRawJsonCtx();
            if (!c) {
                request->_tempObject = reinterpret_cast<void*>(kTempAllocFailedTag);
                return;
            }
            c->busyRejected = true;
            request->_tempObject = c;
            return;
        }
        if (total > HttpPostJson::MAX_BYTES) {
            logger.log(LogModule::WebServer, "%s: body too large (%u > max %u)\n",
                       k == JsonPostStreamKind::Config ? "config" : "program",
                       (unsigned)total, (unsigned)HttpPostJson::MAX_BYTES);
            auto* c = acquireRawJsonCtx();
            if (!c) {
                request->_tempObject = reinterpret_cast<void*>(kTempAllocFailedTag);
                return;
            }
            c->oversize = true;
            c->failed = true;
            request->_tempObject = c;
            return;
        }
        if (!ensureHttpTmpDir()) {
            logger.log(LogModule::WebServer, "%s: ensurePath for http tmp dir failed\n",
                       k == JsonPostStreamKind::Config ? "config" : "program");
            auto* c = acquireRawJsonCtx();
            if (!c) {
                request->_tempObject = reinterpret_cast<void*>(kTempAllocFailedTag);
                return;
            }
            c->failed = true;
            c->ioError = true;
            request->_tempObject = c;
            return;
        }
        auto* c = acquireRawJsonCtx();
        if (!c) {
            request->_tempObject = reinterpret_cast<void*>(kTempAllocFailedTag);
            return;
        }
        c->f = fileSystem.openDirectWrite(jsonPostTmpPath(k), total);
        if (!c->f) {
            logger.log(LogModule::WebServer, "%s: openDirectWrite(%s) failed\n",
                       k == JsonPostStreamKind::Config ? "config" : "program", jsonPostTmpPath(k));
            c->failed = true;
            c->ioError = true;
            request->_tempObject = c;
            return;
        }
        logger.log(LogModule::WebServer, "%s: streaming body to %s (~%u bytes, heap=%u)\n",
                   k == JsonPostStreamKind::Config ? "config" : "program",
                   jsonPostTmpPath(k), (unsigned)total, espHalFreeHeap());
        jsonPostSetThisPending(k, true);
        c->heldPending = true;
        request->_tempObject = c;
    }

    if (request->_tempObject == reinterpret_cast<void*>(kTempAllocFailedTag)) {
        return;
    }
    auto* ctx = static_cast<RawJsonPostCtx*>(request->_tempObject);
    if (!ctx || ctx->busyRejected || ctx->oversize) {
        return;
    }
    if (len == 0) {
        return;
    }
    if (index > (size_t)-1 - len || (total > 0 && index + len > total) ||
        index + len > HttpPostJson::MAX_BYTES) {
        logger.log(LogModule::WebServer, "%s: body length overflow index=%u len=%u total=%u\n",
                   k == JsonPostStreamKind::Config ? "config" : "program",
                   (unsigned)index, (unsigned)len, (unsigned)total);
        ctx->failed = true;
        ctx->oversize = true;
        return;
    }
    if (ctx->f && ctx->f.write(data, len) != len) {
        logger.log(LogModule::WebServer, "%s: chunk write failed to %s\n",
                   k == JsonPostStreamKind::Config ? "config" : "program", jsonPostTmpPath(k));
        ctx->failed = true;
        ctx->ioError = true;
    }
}

void WebServer::jsonPostStreamOnRequest(JsonPostStreamKind k, AsyncWebServerRequest* request) {
    if (request->_tempObject == reinterpret_cast<void*>(kTempAllocFailedTag)) {
        request->_tempObject = nullptr;
        request->send(500, "application/json", "{\"error\":\"Out of memory\"}");
        return;
    }
    auto* ctx = static_cast<RawJsonPostCtx*>(request->_tempObject);
    if (ctx && ctx->heldPending) jsonPostSetThisPending(k, false);
    const char* tmp = jsonPostTmpPath(k);
    if (!ctx) {
        logger.log(LogModule::WebServer, "%s: empty POST body\n",
                   k == JsonPostStreamKind::Config ? "config" : "program");
        if (k == JsonPostStreamKind::Config) {
            request->send(400, "application/json",
                          "{\"success\":false,\"errors\":[{\"field\":\"general\",\"message\":\"Empty body\"}]}");
        } else {
            request->send(400, "application/json", "{\"error\":\"Empty body\"}");
        }
        return;
    }
    if (ctx->f) {
        ctx->f.close();
    }
    if (ctx->busyRejected) {
        logger.log(LogModule::WebServer, "%s: sending BUSY to client\n",
                   k == JsonPostStreamKind::Config ? "config" : "program");
        releaseRawJsonCtx(ctx);
        request->_tempObject = nullptr;
        sendBusy(request);
        return;
    }
    if (ctx->oversize) {
        logger.log(LogModule::WebServer, "%s: removed tmp after 413 oversize\n",
                   k == JsonPostStreamKind::Config ? "config" : "program");
        fileSystem.deleteFile(tmp);
        releaseRawJsonCtx(ctx);
        request->_tempObject = nullptr;
        if (k == JsonPostStreamKind::Config) {
            request->send(413, "application/json",
                          "{\"success\":false,\"errors\":[{\"field\":\"general\",\"message\":\"Payload too large\"}]}");
        } else {
            request->send(413, "application/json", "{\"error\":\"Payload too large\"}");
        }
        return;
    }
    if (ctx->ioError || ctx->failed) {
        logger.log(LogModule::WebServer, "%s: write error, removed tmp %s\n",
                   k == JsonPostStreamKind::Config ? "config" : "program", tmp);
        fileSystem.deleteFile(tmp);
        releaseRawJsonCtx(ctx);
        request->_tempObject = nullptr;
        if (k == JsonPostStreamKind::Config) {
            request->send(500, "application/json",
                          "{\"success\":false,\"errors\":[{\"field\":\"general\",\"message\":\"Write error\"}]}");
        } else {
            request->send(500, "application/json", "{\"error\":\"Write error\"}");
        }
        return;
    }

    logger.log(LogModule::WebServer, "%s: tmp %s closed, deferred apply queued for loop\n",
               k == JsonPostStreamKind::Config ? "config" : "program", tmp);
    releaseRawJsonCtx(ctx);
    request->_tempObject = nullptr;
    jsonPostSetDeferred(k);
    sendJsonSuccess(request);
}

void WebServer::registerJsonPostBodyToTmp(AsyncWebServer& srv, const char* path, JsonPostStreamKind kind) {
    srv.on(path, HTTP_POST,
           [kind](AsyncWebServerRequest* request) { WebServer::jsonPostStreamOnRequest(kind, request); },
           nullptr,
           [kind](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
               WebServer::jsonPostStreamOnBody(kind, request, data, len, index, total);
           });
}

