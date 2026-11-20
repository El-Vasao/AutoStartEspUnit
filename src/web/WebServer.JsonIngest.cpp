// src/web/WebServer.JsonIngest.cpp
#include "web/WebServer.h"
#include "common/EspHal.h"
#include "core/FlashCommitCoordinator.h"
#include "common/Logger.h"
#include "web/internal/WebServerInternal.h"
#include <ESPAsyncWebServer.h>

namespace {
static void sendJsonSuccess(AsyncWebServerRequest* request) {
    AsyncWebServerResponse* resp = request->beginResponse(200, web_internal::kContentTypeJson, "{\"success\":true}");
    request->send(resp);
}

/// Флаги одного POST. Байты тела лежат в FlashCommitCoordinator, не в файле.
struct RawJsonPostCtx {
    bool failed{false};
    bool busyRejected{false};
    bool oversize{false};
    bool ioError{false};
    bool heldPending{false};
    bool ownsBody{false};
    bool committed{false};
    bool settled{false};
    uint8_t slot{0};
    uint8_t gen{0};
};

static constexpr uintptr_t kTempAllocFailedTag = 0x1u;
static constexpr uintptr_t kClosedTag = 0x2u;

static RawJsonPostCtx gRawJsonPostPool[2];
static bool gRawJsonPostUsed[2]{false, false};
static uint8_t gRawJsonPostGen[2]{0, 0};

static RawJsonPostCtx* acquireRawJsonCtx() {
    for (int i = 0; i < 2; i++) {
        if (!gRawJsonPostUsed[i]) {
            gRawJsonPostUsed[i] = true;
            gRawJsonPostPool[i] = RawJsonPostCtx{};
            gRawJsonPostPool[i].slot = static_cast<uint8_t>(i);
            gRawJsonPostPool[i].gen = gRawJsonPostGen[i];
            return &gRawJsonPostPool[i];
        }
    }
    return nullptr;
}

static void releaseRawJsonCtx(RawJsonPostCtx* ctx) {
    if (!ctx) return;
    const uint8_t slot = ctx->slot;
    if (slot < 2 && ctx == &gRawJsonPostPool[slot]) {
        gRawJsonPostGen[slot]++;
        gRawJsonPostUsed[slot] = false;
    }
}

static const char* postLabel(bool isConfig) {
    return isConfig ? "config" : "program";
}

/// keepBody: успешный POST — буфер заберёт tick(). Pending снимает вызывающий (член класса).
static void finishCtx(RawJsonPostCtx* ctx, bool keepBody) {
    if (!ctx || ctx->settled) return;
    ctx->settled = true;
    ctx->heldPending = false;
    if (!keepBody && ctx->ownsBody) flashCommit.dropPostedBody();
    releaseRawJsonCtx(ctx);
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
    // Только RAM: задача async TCP не должна открывать LittleFS параллельно с Core::update.
    if (index == 0) {
        auto reject = [&](bool busy, bool oversize, bool io) -> RawJsonPostCtx* {
            auto* c = acquireRawJsonCtx();
            if (!c) {
                request->_tempObject = reinterpret_cast<void*>(kTempAllocFailedTag);
                return nullptr;
            }
            c->busyRejected = busy;
            c->oversize = oversize;
            c->ioError = io;
            c->failed = oversize || io;
            request->_tempObject = c;
            request->onDisconnect([request, slot = c->slot, gen = c->gen, k]() {
                if (slot >= 2 || gRawJsonPostGen[slot] != gen) return;
                RawJsonPostCtx* ctx = &gRawJsonPostPool[slot];
                if (ctx->settled || ctx->committed) return;
                if (ctx->heldPending) WebServer::jsonPostSetThisPending(k, false);
                finishCtx(ctx, false);
                // onRequest ещё может прийти: не оставлять указатель на переиспользованный слот.
                if (request->_tempObject == ctx) {
                    request->_tempObject = reinterpret_cast<void*>(kClosedTag);
                }
            });
            return c;
        };

        if (jsonPostOtherSidePending(k) || webServer.isFlashBusy()) {
            logger.log(LogModule::WebServer, "%s: rejected, %s\n", postLabel(k == JsonPostStreamKind::Config),
                       jsonPostOtherSidePending(k) ? "other JSON stream active" : "isFlashBusy");
            reject(true, false, false);
            return;
        }
        if (total == 0 || total > HttpPostJson::MAX_BYTES) {
            logger.log(LogModule::WebServer, "%s: body too large or empty declaration (%u, max %u)\n",
                       postLabel(k == JsonPostStreamKind::Config), (unsigned)total, (unsigned)HttpPostJson::MAX_BYTES);
            reject(false, true, false);
            return;
        }
        if (!flashCommit.armPostedBody(k == JsonPostStreamKind::Config, total)) {
            logger.log(LogModule::WebServer, "%s: POST RAM slot busy\n", postLabel(k == JsonPostStreamKind::Config));
            reject(true, false, false);
            return;
        }
        auto* c = reject(false, false, false);
        if (!c) {
            flashCommit.dropPostedBody();
            return;
        }
        c->failed = false;
        c->ownsBody = true;
        jsonPostSetThisPending(k, true);
        c->heldPending = true;
        logger.log(LogModule::WebServer, "%s: body in RAM (~%u bytes, heap=%u), loop will write tmp\n",
                   postLabel(k == JsonPostStreamKind::Config), (unsigned)total, espHalFreeHeap());
    }

    if (request->_tempObject == reinterpret_cast<void*>(kTempAllocFailedTag)) return;
    auto* ctx = static_cast<RawJsonPostCtx*>(request->_tempObject);
    if (!ctx || ctx->busyRejected || ctx->oversize || ctx->failed) return;
    if (len == 0) return;
    if (index > (size_t)-1 - len || (total > 0 && index + len > total) ||
        index + len > HttpPostJson::MAX_BYTES) {
        logger.log(LogModule::WebServer, "%s: body length overflow index=%u len=%u total=%u\n",
                   postLabel(k == JsonPostStreamKind::Config), (unsigned)index, (unsigned)len, (unsigned)total);
        ctx->failed = true;
        ctx->oversize = true;
        return;
    }
    if (!ctx->ownsBody || !flashCommit.appendPostedBody(index, data, len)) {
        logger.log(LogModule::WebServer, "%s: failed to copy chunk to RAM\n", postLabel(k == JsonPostStreamKind::Config));
        ctx->failed = true;
        ctx->ioError = true;
    }
}

void WebServer::jsonPostStreamOnRequest(JsonPostStreamKind k, AsyncWebServerRequest* request) {
    if (request->_tempObject == reinterpret_cast<void*>(kClosedTag)) {
        request->_tempObject = nullptr;
        return;
    }
    if (request->_tempObject == reinterpret_cast<void*>(kTempAllocFailedTag)) {
        request->_tempObject = nullptr;
        request->send(500, "application/json", "{\"error\":\"Out of memory\"}");
        return;
    }
    auto* ctx = static_cast<RawJsonPostCtx*>(request->_tempObject);
    if (ctx && ctx->settled) {
        request->_tempObject = nullptr;
        return;
    }
    if (!ctx) {
        logger.log(LogModule::WebServer, "%s: empty POST body\n", postLabel(k == JsonPostStreamKind::Config));
        if (k == JsonPostStreamKind::Config) {
            request->send(400, "application/json",
                          "{\"success\":false,\"errors\":[{\"field\":\"general\",\"message\":\"Empty body\"}]}");
        } else {
            request->send(400, "application/json", "{\"error\":\"Empty body\"}");
        }
        return;
    }
    if (ctx->busyRejected) {
        logger.log(LogModule::WebServer, "%s: BUSY sent to client\n", postLabel(k == JsonPostStreamKind::Config));
        if (ctx->heldPending) jsonPostSetThisPending(k, false);
        finishCtx(ctx, false);
        request->_tempObject = nullptr;
        web_internal::sendBusy(request);
        return;
    }
    if (ctx->oversize) {
        logger.log(LogModule::WebServer, "%s: body larger than RAM slot\n", postLabel(k == JsonPostStreamKind::Config));
        if (ctx->heldPending) jsonPostSetThisPending(k, false);
        finishCtx(ctx, false);
        request->_tempObject = nullptr;
        if (k == JsonPostStreamKind::Config) {
            request->send(413, "application/json",
                          "{\"success\":false,\"errors\":[{\"field\":\"general\",\"message\":\"Payload too large\"}]}");
        } else {
            request->send(413, "application/json", "{\"error\":\"Payload too large\"}");
        }
        return;
    }
    if (ctx->ioError || ctx->failed || !flashCommit.postedBodyComplete()) {
        logger.log(LogModule::WebServer, "%s: body in RAM not assembled, tmp not written\n", postLabel(k == JsonPostStreamKind::Config));
        if (ctx->heldPending) jsonPostSetThisPending(k, false);
        finishCtx(ctx, false);
        request->_tempObject = nullptr;
        if (k == JsonPostStreamKind::Config) {
            request->send(500, "application/json",
                          "{\"success\":false,\"errors\":[{\"field\":\"general\",\"message\":\"Write error\"}]}");
        } else {
            request->send(500, "application/json", "{\"error\":\"Write error\"}");
        }
        return;
    }

    logger.log(LogModule::WebServer, "%s: RAM closed, deferred write %s queued in loop\n",
               postLabel(k == JsonPostStreamKind::Config), jsonPostTmpPath(k));
    ctx->committed = true;
    jsonPostSetDeferred(k);
    if (ctx->heldPending) jsonPostSetThisPending(k, false);
    finishCtx(ctx, true);
    request->_tempObject = nullptr;
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
