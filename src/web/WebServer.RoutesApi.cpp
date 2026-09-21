/**
 * @file WebServer.RoutesApi.cpp
 * @brief HTTP API маршруты (JSON) подсистемы WebServer.
 *
 * Ответственность:
 * - Регистрация API-эндпоинтов, которые управляют конфигом/программами/рантайм-флагами.
 * - Формирование компактных JSON ответов без лишней нагрузки на heap.
 *
 * Инварианты по памяти:
 * - Не делать `String`-конкатенаций в циклах.
 * - JSON body: `sendJsonBuffered` (один emit в буфер).
 *
 * Запрещено:
 * - Подключать внутренние заголовки web-подсистемы из других подсистем.
 */
#include "web/WebServer.h"
#include "common/EspHal.h"
#include "web/internal/WebServerInternal.h"
#include "web/internal/WebServerRuntime.h"
#include "core/Core.h"
#include "config/Config.h"
#include "core/FlashCommitCoordinator.h"
#include "io/DigitalInputs.h"
#include "gsm/GSMController.h"
#include "common/Pins.h"
#include "program/ProgramExecutor.h"
#include "io/SensorsController.h"

using namespace web_internal;

namespace {
volatile bool gOtaHttpUploadAwaitTimedOut = false;
volatile bool gBootstrapInFlight = false;

void emitBootstrapJson(Print& p) {
    p.print("{\"version\":\"");
    p.print(core.getVersionString());
    p.print("\",\"uiLease\":{\"heartbeatMs\":");
    p.print(WebUi::HEARTBEAT_INTERVAL_MS);
    p.print(",\"sessionTimeoutMs\":");
    p.print(WebUi::SESSION_TIMEOUT_MS);
    p.print("},\"hwCounts\":{\"relays\":");
    p.print(HardwareLimits::RELAYS);
    p.print(",\"inputs\":");
    p.print(HardwareLimits::INPUTS);
    p.print(",\"sensors\":");
    p.print(HardwareLimits::SENSORS);
    p.print("},\"hwMap\":{\"inputs\":[");

    for (uint8_t i = 0; i < HardwareLimits::INPUTS; i++) {
        if (i) p.print(',');
        p.print("{\"id\":");
        p.print(Pin::INPUT_IDS[i]);
        p.print(",\"label\":\"IN");
        p.print(i + 1);
        p.print("\"}");
    }

    p.print("],\"relays\":[");
    for (uint8_t i = 0; i < HardwareLimits::RELAYS; i++) {
        if (i) p.print(',');
        p.print("{\"id\":");
        p.print(Pin::RELAY_IDS[i]);
        p.print(",\"label\":\"K");
        p.print(i + 1);
        p.print("\"}");
    }

    p.print("]},\"temperatureSensorRoms\":[");
    const uint8_t n = core.getSensors().getSensorCount();
    for (uint8_t i = 0; i < n; i++) {
        if (i) p.print(',');
        const uint8_t* addr = core.getSensors().getSensorAddress(i);
        if (!addr) {
            p.print("null");
            continue;
        }
        char romStr[TextBytes::Sensors::ADDR_STRING];
        snprintf(romStr, sizeof(romStr), "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                 addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7]);
        p.print('\"');
        p.print(romStr);
        p.print('\"');
    }
    p.print("],\"live\":");
    WebServerRuntime::emitLiveSnapshotJson(p);
    p.print('}');
}
} // namespace

void WebServer::markOtaHttpUploadAwaitTimedOut() { gOtaHttpUploadAwaitTimedOut = true; }

static bool readEnabledBodyParam(AsyncWebServerRequest* request, bool* outEn) {
    if (!request->hasParam("enabled", true)) return false;
    *outEn = request->getParam("enabled", true)->value().equalsIgnoreCase("true");
    return true;
}

static bool readIdEnabledBodyParams(AsyncWebServerRequest* request, uint32_t* idOut, bool* enOut) {
    if (!request->hasParam("id", true) || !request->hasParam("enabled", true)) return false;
    const String& value = request->getParam("id", true)->value();
    if (value.length() == 0) return false;
    char* end = nullptr;
    const unsigned long parsed = strtoul(value.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    *idOut = static_cast<uint32_t>(parsed);
    *enOut = request->getParam("enabled", true)->value().equalsIgnoreCase("true");
    return true;
}

void WebServer::setupApiRoutes_() {
    server.on("/config/get", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (rejectIfFlashBusy(request)) return;
        sendJsonFromFs(request, "/config.json", "no-cache, no-store, must-revalidate");
    });

    server.on("/bootstrap", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (gBootstrapInFlight) {
            request->send(429, kContentTypeJson, "{\"success\":false,\"error\":\"BOOTSTRAP_BUSY\"}");
            return;
        }
        gBootstrapInFlight = true;
        const uint32_t startedAt = millis();
        if (!sendJsonBuffered(request, emitBootstrapJson)) {
            gBootstrapInFlight = false;
            return;
        }
        logger.log("[WebServer] /bootstrap done in %lu ms (heap free=%u max=%u)\n",
                   (unsigned long)(millis() - startedAt),
                   (unsigned)espHalFreeHeap(),
                   (unsigned)espHalMaxBlock());
        gBootstrapInFlight = false;
    });

    server.on("/ui/session", HTTP_POST, [](AsyncWebServerRequest* request) {
        uint32_t sessionId = 0;
        if (!parseUint32Param(request, "id", &sessionId) || sessionId == 0) {
            request->send(400, kContentTypeJson, "{\"success\":false,\"error\":\"INVALID_SESSION_ID\"}");
            return;
        }

        const bool closing = request->hasParam("close", true) &&
            request->getParam("close", true)->value().equalsIgnoreCase("1");
        const uint16_t prevCount = webServer.activeUiSessionCount();
        const bool ok = closing
            ? webServer.closeUiSession(sessionId)
            : webServer.touchUiSession(sessionId, millis());
        const uint16_t newCount = webServer.activeUiSessionCount();

        if (closing) {
            if (ok) {
                logger.log("[WebServer] UI session closed: id=%lu\n", static_cast<unsigned long>(sessionId));
            }
            if (ok && prevCount > 0 && newCount == 0) {
                const size_t q = webServer.events.avgPacketsWaiting();
                const uint16_t clients = webServer.refreshSseClientCount();
                if (clients > 0 || q > 0) {
                    logger.log("[WebServer] UI sessions=0 (explicit close): closing SSE (clients=%u, queue=%u)\n",
                               (unsigned)clients, (unsigned)q);
                    webServer.events.close();
                    webServer.sseClientCount = 0;
                    webServer.lastObservedSseClients_ = 0;
                    webServer.sseDiagTailUntilMs_ = 0;
                }
            }
        }

        if (!ok && !closing) {
            request->send(503, kContentTypeJson, "{\"success\":false,\"error\":\"UI_SESSION_POOL_FULL\"}");
            return;
        }

        sendJsonSuccess(request);
    });

    registerJsonPostBodyToTmp(server, "/config/save", JsonPostStreamKind::Config);

    server.on("/config/reset", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (rejectIfFlashBusy(request)) return;
        flashCommit.queueResetConfig();
        sendJsonSuccess(request);
    });

    // OTA upload
    struct UploadContext {
        File file;
        size_t totalSize{0};
        bool errored{false};
    };
    static UploadContext gUploadCtx;
    static bool gUploadCtxUsed = false;

    auto acquireUploadCtx = []() -> UploadContext* {
        if (gUploadCtxUsed) return nullptr;
        gUploadCtxUsed = true;
        gUploadCtx = UploadContext{};
        return &gUploadCtx;
    };
    auto releaseUploadCtx = [](UploadContext* ctx) {
        if (!ctx) return;
        if (ctx == &gUploadCtx) gUploadCtxUsed = false;
    };

    server.on("/upload", HTTP_POST, [](AsyncWebServerRequest* request) {},
        [=](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (index == 0 && rejectIfFlashBusy(request)) {
                return;
            }
            if (index != 0 && gOtaHttpUploadAwaitTimedOut) {
                auto* timedCtx = static_cast<UploadContext*>(request->_tempObject);
                if (timedCtx && timedCtx->file) {
                    fileSystem.closeWriteStream(timedCtx->file, "/update.bin", false);
                }
                if (timedCtx) {
                    releaseUploadCtx(timedCtx);
                    request->_tempObject = nullptr;
                }
                gOtaHttpUploadAwaitTimedOut = false;
                if (fileSystem.exists("/update.bin")) {
                    fileSystem.deleteFile("/update.bin");
                }
                request->send(408, kContentTypeText, "OTA upload timed out");
                return;
            }

            auto* ctx = static_cast<UploadContext*>(request->_tempObject);

            if (!index) {
                gOtaHttpUploadAwaitTimedOut = false;
                logger.log("[WebServer] Upload start: %s\n", filename.c_str());

                // If we have leftover context, free it.
                if (ctx) {
                    if (ctx->file) {
                        fileSystem.closeWriteStream(ctx->file, "/update.bin", false);
                    }
                    releaseUploadCtx(ctx);
                    ctx = nullptr;
                    request->_tempObject = nullptr;
                }

                if (fileSystem.exists("/update.bin")) {
                    fileSystem.deleteFile("/update.bin");
                }
                if (fileSystem.exists("/update.bin.tmp")) {
                    fileSystem.deleteFile("/update.bin.tmp");
                }

                size_t freeSpace = fileSystem.getFreeSpace();
                logger.log("[WebServer] Free space before upload: %u bytes\n", (unsigned)freeSpace);
                if (freeSpace < OTA::FILE_MAX_SIZE + FSystem::MIN_FREE_SPACE) {
                    logger.log("[WebServer] Low space, running GC...\n");
                    fileSystem.gc();
                    freeSpace = fileSystem.getFreeSpace();
                    if (freeSpace < OTA::FILE_MAX_SIZE + FSystem::MIN_FREE_SPACE) {
                        logger.log("[WebServer] Still insufficient space\n");
                        request->send(507, kContentTypeText, "Insufficient space");
                        return;
                    }
                }

                ctx = acquireUploadCtx();
                if (!ctx) {
                    request->send(409, kContentTypeText, "Busy");
                    return;
                }
                ctx->totalSize = 0;
                ctx->errored = false;
                ctx->file = fileSystem.openWriteStream("/update.bin");
                request->_tempObject = ctx;

                if (!ctx->file) {
                    logger.log("[WebServer] Failed to open /update.bin for writing\n");
                    request->send(500, kContentTypeText, "Failed to open file");
                    releaseUploadCtx(ctx);
                    request->_tempObject = nullptr;
                    return;
                }
                core.onOtaHttpUploadStreamOpenedFromWeb();
            }

            if (!ctx) {
                request->send(400, kContentTypeText, "Upload context missing");
                return;
            }
            if (ctx->errored) {
                return;
            }

            if (len > 0) {
                if (ctx->file.write(data, len) != len) {
                    logger.log("[WebServer] Write error during upload\n");
                    fileSystem.closeWriteStream(ctx->file, "/update.bin", false);
                    ctx->errored = true;
                    core.notifyOtaHttpUploadComplete(false);
                    request->send(500, kContentTypeText, "Write error");
                    return;
                }
                ctx->totalSize += len;
                if (ctx->totalSize > OTA::FILE_MAX_SIZE) {
                    logger.log("[WebServer] Upload rejected: too large (%u > %u)\n",
                               (unsigned)ctx->totalSize, (unsigned)OTA::FILE_MAX_SIZE);
                    fileSystem.closeWriteStream(ctx->file, "/update.bin", false);
                    ctx->errored = true;
                    core.notifyOtaHttpUploadComplete(false);
                    request->send(413, kContentTypeText, "Payload too large");
                    releaseUploadCtx(ctx);
                    request->_tempObject = nullptr;
                    return;
                }
            }

            if (final) {
                logger.log("[WebServer] Upload finished, total size: %u\n", (unsigned)ctx->totalSize);
                if (!fileSystem.closeWriteStream(ctx->file, "/update.bin", true)) {
                    logger.log("[WebServer] Failed to finalize /update.bin\n");
                    core.notifyOtaHttpUploadComplete(false);
                    request->send(500, kContentTypeText, "Failed to finalize file");
                    releaseUploadCtx(ctx);
                    request->_tempObject = nullptr;
                    return;
                }
                core.notifyOtaHttpUploadComplete(true);
                request->send(200, kContentTypeText, "Upload OK");
                releaseUploadCtx(ctx);
                request->_tempObject = nullptr;
            }
        });

    server.on("/ota/start", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (rejectIfFlashBusy(request)) return;
        if (!fileSystem.exists("/update.bin")) {
            request->send(400, kContentTypeJson, "{\"success\":false,\"message\":\"No update file found\"}");
            return;
        }
        sendJsonSuccess(request);
        core.startOTAUpdate();
        webServer.broadcastStatusForce();
    });

    server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(200, kContentTypeText, "Rebooting...");
        core.requestReboot(Delays::REBOOT_HTTP_REPLY_MS);
    });

    server.on("/modem/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(200, kContentTypeText, "Modem reboot requested");
        core.getGSM().requestModemReboot();
        webServer.broadcastStatusForce();
    });

    server.on("/programs", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (rejectIfFlashBusy(request)) return;
        sendJsonFromFs(request, "/programs/index.json", "no-cache, no-store, must-revalidate");
    });
    server.on("/programs", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (rejectIfFlashBusy(request)) return;
        if (request->hasParam("reset", true) && request->getParam("reset", true)->value() == "1") {
            flashCommit.queueResetPrograms();
            sendJsonSuccess(request);
            return;
        }
        if (request->hasParam("run", true)) {
            uint8_t id = request->getParam("run", true)->value().toInt();
            if (core.getProgramExecutor().start(id)) {
                sendJsonSuccess(request);
            } else {
                request->send(400, kContentTypeJson, "{\"success\":false}");
            }
            return;
        }
        request->send(400, kContentTypeText, "Expected reset=1 or run=<id> in POST body");
    });

    server.on("/program", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (rejectIfFlashBusy(request)) return;
        if (!request->hasParam("id")) {
            request->send(400, kContentTypeText, "Missing id");
            return;
        }
        uint8_t id = request->getParam("id")->value().toInt();
        char path[BufferBytes::Fs::PROGRAM_PATH];
        snprintf(path, sizeof(path), "/programs/%u.json", id);
        sendJsonFromFs(request, path, "no-cache, no-store, must-revalidate");
    });

    registerJsonPostBodyToTmp(server, "/program", JsonPostStreamKind::Program);

    server.on("/program", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (rejectIfFlashBusy(request)) return;
        if (!request->hasParam("id")) {
            request->send(400, kContentTypeText, "Missing id");
            return;
        }
        uint8_t id = request->getParam("id")->value().toInt();
        flashCommit.queueDeleteProgram(id);
        sendJsonSuccess(request);
    });

    server.on("/runtime", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("op", true)) {
            request->send(400, kContentTypeText, "Missing op");
            return;
        }
        const String& op = request->getParam("op", true)->value();
        if (op == "thermostat") {
            bool en = false;
            if (!readEnabledBodyParam(request, &en)) {
                request->send(400, kContentTypeText, "Missing enabled");
                return;
            }
            core.setThermostatRuntime(en);
            sendJsonSuccess(request);
            return;
        }
        if (op == "batterysaver") {
            bool en = false;
            if (!readEnabledBodyParam(request, &en)) {
                request->send(400, kContentTypeText, "Missing enabled");
                return;
            }
            core.setBatterySaverRuntime(en);
            sendJsonSuccess(request);
            return;
        }
        if (op == "input_id") {
            uint32_t id = 0;
            bool en = false;
            if (!readIdEnabledBodyParams(request, &id, &en)) {
                request->send(400, kContentTypeText, "Missing parameters");
                return;
            }
            const int8_t idx = core.getInputs().findIndexById((uint16_t)id);
            if (idx < 0) {
                request->send(400, kContentTypeText, "Input id not found");
                return;
            }
            core.getInputs().setRuntimeEnabled((uint8_t)idx, en);
            sendJsonSuccess(request);
            return;
        }
        // Примечание: операции вида trigger_* (по id) пока не перенесены в этот роут,
        // чтобы не смешивать крупные изменения API. Если понадобится — перенесём вместе с
        // унификацией схемы параметров и строгой валидацией.
        request->send(400, kContentTypeText, "Invalid op");
    });
}

