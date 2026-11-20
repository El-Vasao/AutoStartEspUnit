// src/web/WebRamAssets.cpp
// app.js.gz ~122 КБ не держим в куче: вместе с SoftAP это съедает остаток DRAM ESP32-C3.
// html/css gzip и схемы ≤16 КБ предзагружаются; остальное — окно 4 КБ, которое наполняет цикл.
#include "web/internal/WebRamAssets.h"

#include "common/Constants.h"
#include "common/EspHal.h"
#include "common/Logger.h"
#include "fs/FsManager.h"

#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstring>
#include <stdlib.h>

namespace web_ram {

namespace {

constexpr size_t kStreamWindow = 4096;
constexpr size_t kPreloadMax = 16 * 1024;
constexpr uint32_t kWaitMs = 5000;

static_assert(kStreamWindow <= Limits::CONFIG_JSON_SIZE, "окно потока больше scratch");

enum class Job : uint8_t { None, Small, StreamOpen, StreamRead };
enum class Hold : uint8_t { Free, Scratch, Stream };

struct Blob {
    const char* path;
    uint8_t* data;
    size_t len;
};

// Логические пути на ФС. app.js.gz и settings.ui.json (~36 КБ) сюда не входят — отдача окном.
const char* const kPreloadPaths[] = {
    "/index.html.gz",
    "/style.css.gz",
    "/ui/program-steps.ui.json",
    "/validation/program.schema.json",
    "/validation/programs.index.schema.json",
    "/validation/settings.schema.json",
};

SemaphoreHandle_t sNeed = nullptr;
SemaphoreHandle_t sReady = nullptr;

Blob gBlobs[8]{};
size_t gBlobCount = 0;
bool gPreloaded = false;

uint8_t gScratch[Limits::CONFIG_JSON_SIZE];
size_t gSmallLen = 0;
size_t gFileSize = 0;
size_t gWinLen = 0;
size_t gWinPos = 0;

char gPath[BufferBytes::Fs::GZIP_PATH]{};
Job gJob = Job::None;
Hold gHold = Hold::Free;
File gFile;
bool gOk = false;
bool gMissing = false;
bool gTooBig = false;
bool gEof = false;
volatile bool gCancel = false;
volatile bool gWaiting = false;

const Blob* findBlob(const char* path) {
    for (size_t i = 0; i < gBlobCount; i++) {
        if (gBlobs[i].path && strcmp(gBlobs[i].path, path) == 0) return &gBlobs[i];
    }
    return nullptr;
}

bool endsWith(const char* s, const char* suf) {
    if (!s || !suf) return false;
    const size_t ls = strlen(s);
    const size_t lf = strlen(suf);
    return ls >= lf && strcmp(s + (ls - lf), suf) == 0;
}

void ensureSems() {
    if (!sNeed) sNeed = xSemaphoreCreateBinary();
    if (!sReady) sReady = xSemaphoreCreateBinary();
}

void releaseScratch() {
    if (gHold == Hold::Scratch) gHold = Hold::Free;
}

void cancelStream() {
    // Файл закроет цикл: отсюда LittleFS звать нельзя.
    if (gHold == Hold::Stream) gCancel = true;
}

void addGzip(AsyncWebServerResponse* resp) {
    resp->addHeader("Content-Encoding", "gzip");
    resp->addHeader("Vary", "Accept-Encoding");
}

void preload() {
    if (gPreloaded || !fileSystem.isInitialized()) return;
    // Один раз на цикле, до server.begin: убрать осиротевшие tmp и обновить снимок места.
    fileSystem.gc();
    gBlobCount = 0;
    for (const char* path : kPreloadPaths) {
        if (gBlobCount >= (sizeof(gBlobs) / sizeof(gBlobs[0]))) break;
        File f = fileSystem.openRead(path);
        if (!f) continue;
        const size_t n = f.size();
        if (n == 0 || n > kPreloadMax) {
            f.close();
            if (n > kPreloadMax) {
                logger.log(LogModule::WebServer, "RAM UI: %s too large (%u), serving by window\n", path,
                           (unsigned)n);
            }
            continue;
        }
        uint8_t* buf = static_cast<uint8_t*>(malloc(n));
        if (!buf) {
            f.close();
            logger.log(LogModule::WebServer, "RAM UI: no heap for %s (%u)\n", path, (unsigned)n);
            continue;
        }
        const size_t got = f.read(buf, n);
        f.close();
        if (got != n) {
            free(buf);
            continue;
        }
        gBlobs[gBlobCount++] = Blob{path, buf, n};
    }
    gPreloaded = true;
    logger.log(LogModule::WebServer, "RAM UI: preloaded %u files (heap=%u)\n", (unsigned)gBlobCount,
               (unsigned)espHalFreeHeap());
}

void jobSmall() {
    File f = fileSystem.openRead(gPath);
    if (!f) {
        gMissing = true;
        gOk = false;
        return;
    }
    const size_t n = f.size();
    if (n == 0 || n > sizeof(gScratch)) {
        gTooBig = n > sizeof(gScratch);
        gMissing = (n == 0);
        f.close();
        gOk = false;
        return;
    }
    const size_t got = f.read(gScratch, n);
    f.close();
    gSmallLen = got;
    gOk = (got == n);
    if (!gOk) gMissing = true;
}

void jobStreamOpen() {
    gFile = fileSystem.openRead(gPath);
    gEof = false;
    gWinPos = 0;
    gWinLen = 0;
    if (!gFile) {
        gMissing = true;
        gOk = false;
        return;
    }
    gFileSize = gFile.size();
    const size_t n = gFile.read(gScratch, kStreamWindow);
    if (n == 0) {
        gFile.close();
        gEof = true;
    } else {
        gWinLen = n;
    }
    gOk = true;
}

void jobStreamRead() {
    gWinPos = 0;
    gWinLen = 0;
    if (!gFile) {
        gEof = true;
        gOk = true;
        return;
    }
    const size_t n = gFile.read(gScratch, kStreamWindow);
    if (n == 0) {
        gFile.close();
        gEof = true;
    } else {
        gWinLen = n;
    }
    gOk = true;
}

void closeStreamFile() {
    if (gFile) gFile.close();
    gEof = true;
    gHold = Hold::Free;
    gCancel = false;
}

void pump() {
    if (!sNeed) return;
    if (xSemaphoreTake(sNeed, 0) != pdTRUE) {
        // Клиент ушёл между чанками: файл ещё открыт, async уже не ждёт.
        if (gCancel && !gWaiting) closeStreamFile();
        return;
    }
    if (gCancel) {
        closeStreamFile();
        gOk = false;
        xSemaphoreGive(sReady);
        return;
    }
    switch (gJob) {
        case Job::Small: jobSmall(); break;
        case Job::StreamOpen: jobStreamOpen(); break;
        case Job::StreamRead: jobStreamRead(); break;
        default: gOk = false; break;
    }
    xSemaphoreGive(sReady);
}

bool runJob(Job job, const char* path) {
    ensureSems();
    if (!sNeed || !sReady || !path || path[0] == '\0') return false;
    while (xSemaphoreTake(sReady, 0) == pdTRUE) {}
    strlcpy(gPath, path, sizeof(gPath));
    gOk = false;
    gMissing = false;
    gTooBig = false;
    gCancel = false;
    gJob = job;
    gHold = (job == Job::Small) ? Hold::Scratch : Hold::Stream;
    xSemaphoreGive(sNeed);
    if (xSemaphoreTake(sReady, pdMS_TO_TICKS(kWaitMs)) != pdTRUE) {
        gCancel = true;
        (void)xSemaphoreTake(sReady, pdMS_TO_TICKS(1000));
        if (!gFile) {
            gHold = Hold::Free;
            gCancel = false;
        }
        return false;
    }
    if (!gOk) {
        if (!gFile) gHold = Hold::Free;
        return false;
    }
    return true;
}

size_t streamFill(uint8_t* dst, size_t maxLen, size_t index) {
    (void)index;
    if (!dst || maxLen == 0) return 0;
    size_t filled = 0;
    while (filled < maxLen) {
        if (gWinPos >= gWinLen) {
            if (gEof || gCancel) break;
            while (xSemaphoreTake(sReady, 0) == pdTRUE) {}
            gJob = Job::StreamRead;
            gWaiting = true;
            xSemaphoreGive(sNeed);
            const bool woke = xSemaphoreTake(sReady, pdMS_TO_TICKS(kWaitMs)) == pdTRUE;
            gWaiting = false;
            if (!woke || gCancel || !gOk) {
                if (!woke) {
                    logger.log(LogModule::WebServer, "RAM UI: window timeout %s\n", gPath);
                }
                gCancel = true;
                gEof = true;
                gWinLen = 0;
                break;
            }
            gWinPos = 0;
            if (gWinLen == 0) {
                gEof = true;
                break;
            }
        }
        size_t n = gWinLen - gWinPos;
        if (n > maxLen - filled) n = maxLen - filled;
        memcpy(dst + filled, gScratch + gWinPos, n);
        gWinPos += n;
        filled += n;
    }
    if ((gEof || gCancel) && gWinPos >= gWinLen && !gFile) {
        gHold = Hold::Free;
    }
    return filled;
}

} // namespace

void tick() {
    if (!fileSystem.isInitialized()) return;
    ensureSems();
    if (!gPreloaded) preload();
    pump();
}

Take acquire(AsyncWebServerRequest* request, const char* path, const char* contentType, bool gzip,
             AsyncWebServerResponse** out) {
    if (out) *out = nullptr;
    if (!request || !out || !path || path[0] != '/' || !contentType) return Take::Unavailable;

    if (const Blob* blob = findBlob(path)) {
        AsyncWebServerResponse* resp = request->beginResponse(200, contentType, blob->data, blob->len);
        if (!resp) return Take::Unavailable;
        if (gzip) addGzip(resp);
        *out = resp;
        return Take::Ready;
    }

    // Насос один: вторая отдача, пока первый файл ещё в scratch/окне.
    if (gHold != Hold::Free) return Take::Unavailable;

    if (!endsWith(path, ".gz")) {
        if (runJob(Job::Small, path)) {
            AsyncWebServerResponse* resp = request->beginResponse(200, contentType, gScratch, gSmallLen);
            if (!resp) {
                gHold = Hold::Free;
                return Take::Unavailable;
            }
            request->onDisconnect([]() { releaseScratch(); });
            if (gzip) addGzip(resp);
            *out = resp;
            return Take::Ready;
        }
        if (gMissing) return Take::Missing;
        if (!gTooBig || gHold != Hold::Free) return Take::Unavailable;
    }

    if (!runJob(Job::StreamOpen, path)) {
        if (gMissing) return Take::Missing;
        return Take::Unavailable;
    }
    AsyncWebServerResponse* resp = request->beginResponse(contentType, gFileSize, streamFill);
    if (!resp) {
        gCancel = true;
        return Take::Unavailable;
    }
    request->onDisconnect([]() { cancelStream(); });
    if (gzip) addGzip(resp);
    *out = resp;
    return Take::Ready;
}

} // namespace web_ram
