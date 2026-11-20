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
#include "app/AppPorts.h"
#include "core/Core.h"
#include "config/Config.h"
#include "config/SensorRomResolve.h"
#include "config/internal/ProgramJsonIo.h"
#include "core/FlashCommitCoordinator.h"
#include "fs/FsManager.h"
#include "web/internal/WebRamAssets.h"
#include "common/ChannelSilk.h"
#include "common/JsonEscape.h"
#include "common/Pins.h"
#include "io/SensorsController.h"

using namespace web_internal;

namespace {
volatile bool gOtaHttpUploadAwaitTimedOut = false;
volatile bool gBootstrapInFlight = false;

const AppPorts* webPorts() { return webServer.appPorts(); }

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
        ChannelSilk::printQuotedId(p, ChannelSilk::numberFromSlot(i, ChannelSilk::Kind::Input),
                                   ChannelSilk::Kind::Input);
        p.print(",\"label\":");
        json_escape::writeQuoted(p, config.getBase().inputs[i].name);
        p.print('}');
    }

    p.print("],\"relays\":[");
    for (uint8_t i = 0; i < HardwareLimits::RELAYS; i++) {
        if (i) p.print(',');
        p.print("{\"id\":");
        ChannelSilk::printQuotedId(p, ChannelSilk::numberFromSlot(i, ChannelSilk::Kind::Relay),
                                   ChannelSilk::Kind::Relay);
        p.print(",\"label\":");
        json_escape::writeQuoted(p, config.getBase().relays[i].name);
        p.print('}');
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
        formatSensorRomString(addr, romStr, sizeof(romStr));
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
        if (!config.isLoaded()) {
            request->send(503, kContentTypeJson, "{\"success\":false,\"error\":\"NOT_READY\"}");
            return;
        }
        // Кэш Config уже в RAM — файл /config.json из колбэка не открываем.
        sendJsonBuffered(request, [](Print& p) { config.emitBaseJson(p); }, Limits::CONFIG_JSON_SIZE);
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
        logger.log(LogModule::WebServer, "/bootstrap готов за %lu мс (куча свободно=%u макс=%u)\n",
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
                logger.log(LogModule::WebServer, "сессия UI закрыта: id=%lu\n", static_cast<unsigned long>(sessionId));
            }
            if (ok && prevCount > 0 && newCount == 0) {
                const size_t q = webServer.events.avgPacketsWaiting();
                const uint16_t clients = webServer.refreshSseClientCount();
                if (clients > 0 || q > 0) {
                    logger.log(LogModule::WebServer, "сессий UI=0 (явное закрытие): закрываю SSE (клиенты=%u, очередь=%u)\n",
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
        if (!flashCommit.queueResetConfig()) {
            sendBusy(request);
            return;
        }
        sendJsonSuccess(request);
    });

    // Загрузка OTA — поток пакета в Update и ФС (без /update.bin).
    struct UploadContext {
        size_t totalSize{0};
        bool errored{false};
        bool active{false};
    };
    static UploadContext gUploadCtx;

    server.on("/upload", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            // Ответ уходит из обработчика загрузки на final или при ошибке.
            (void)request;
        },
        [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (index == 0 && rejectIfFlashBusy(request)) {
                return;
            }
            if (index != 0 && gOtaHttpUploadAwaitTimedOut) {
                gOtaHttpUploadAwaitTimedOut = false;
                if (const AppPorts* p = webPorts()) {
                    if (p->ota.abort) p->ota.abort(p->ota.ctx);
                    if (p->ota.notifyComplete) p->ota.notifyComplete(p->ota.ctx, false);
                }
                gUploadCtx = UploadContext{};
                request->send(408, kContentTypeText, "OTA upload timed out");
                return;
            }

            if (!index) {
                gOtaHttpUploadAwaitTimedOut = false;
                logger.log(LogModule::WebServer, "старт потоковой загрузки OTA: %s\n", filename.c_str());

                // Снимок места обновляет цикл. LittleFS из этого колбэка не вызываем.
                size_t freeSpace = fileSystem.cachedFreeSpace();
                logger.log(LogModule::WebServer, "свободно перед потоком OTA (снимок): %u байт\n", (unsigned)freeSpace);
                if (freeSpace < Ota::STREAM_FS_HEADROOM + FsSpace::MIN_FREE_SPACE) {
                    logger.log(LogModule::WebServer, "недостаточно запаса ФС для хвоста UI\n");
                    request->send(507, kContentTypeText, "Insufficient space");
                    return;
                }

                if (gUploadCtx.active) {
                    request->send(409, kContentTypeText, "Busy");
                    return;
                }
                gUploadCtx = UploadContext{};
                gUploadCtx.active = true;
                // Контекст async: автомат потока готовится здесь; Core::update выполняет отложенный переход в OTA_UPDATE.
                // См. docs/modules/web.md, раздел Concurrency.
                if (const AppPorts* p = webPorts()) {
                    if (p->ota.onUploadOpened) p->ota.onUploadOpened(p->ota.ctx);
                }
            }

            if (!gUploadCtx.active) {
                request->send(400, kContentTypeText, "Upload context missing");
                return;
            }
            if (gUploadCtx.errored) {
                return;
            }

            if (len > 0) {
                const AppPorts* p = webPorts();
                const bool fed = p && p->ota.feed && p->ota.feed(p->ota.ctx, data, len);
                if (!fed) {
                    logger.log(LogModule::WebServer, "подача потока не удалась на %u байтах\n",
                               (unsigned)(gUploadCtx.totalSize + len));
                    gUploadCtx.errored = true;
                    if (p) {
                        if (p->ota.abort) p->ota.abort(p->ota.ctx);
                        if (p->ota.notifyComplete) p->ota.notifyComplete(p->ota.ctx, false);
                    }
                    gUploadCtx = UploadContext{};
                    request->send(500, kContentTypeText, "Stream error");
                    return;
                }
                gUploadCtx.totalSize += len;
                if (gUploadCtx.totalSize > Ota::FILE_MAX_SIZE) {
                    logger.log(LogModule::WebServer, "загрузка отклонена: слишком велика (%u > %u)\n",
                               (unsigned)gUploadCtx.totalSize, (unsigned)Ota::FILE_MAX_SIZE);
                    gUploadCtx.errored = true;
                    if (p) {
                        if (p->ota.abort) p->ota.abort(p->ota.ctx);
                        if (p->ota.notifyComplete) p->ota.notifyComplete(p->ota.ctx, false);
                    }
                    gUploadCtx = UploadContext{};
                    request->send(413, kContentTypeText, "Payload too large");
                    return;
                }
            }

            if (final) {
                logger.log(LogModule::WebServer, "потоковая загрузка завершена, размер: %u\n",
                           (unsigned)gUploadCtx.totalSize);
                const AppPorts* p = webPorts();
                const bool ok = !gUploadCtx.errored && p && p->ota.finish && p->ota.finish(p->ota.ctx);
                if (!ok) {
                    if (p) {
                        if (p->ota.abort) p->ota.abort(p->ota.ctx);
                        if (p->ota.notifyComplete) p->ota.notifyComplete(p->ota.ctx, false);
                    }
                    gUploadCtx = UploadContext{};
                    request->send(500, kContentTypeText, "Stream finalize failed");
                    return;
                }
                if (p && p->ota.notifyComplete) p->ota.notifyComplete(p->ota.ctx, true);
                gUploadCtx = UploadContext{};
                request->send(200, kContentTypeJson, "{\"success\":true,\"rebooting\":true}");
            }
        });

    server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(200, kContentTypeText, "Rebooting...");
        if (const AppPorts* p = webPorts()) {
            if (p->control.requestReboot) p->control.requestReboot(p->control.ctx, Delays::REBOOT_HTTP_REPLY_MS);
        }
    });

    server.on("/modem/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(200, kContentTypeText, "Modem reboot requested");
        if (const AppPorts* p = webPorts()) {
            if (p->control.requestModemReboot) p->control.requestModemReboot(p->control.ctx);
        }
        webServer.broadcastStatusForce();
    });

    server.on("/programs", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (rejectIfFlashBusy(request)) return;
        AsyncWebServerResponse* resp = nullptr;
        const web_ram::Take take =
            web_ram::acquire(request, "/programs/index.json", kContentTypeJson, false, &resp);
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
    });
    server.on("/programs", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (rejectIfFlashBusy(request)) return;
        if (request->hasParam("reset", true) && request->getParam("reset", true)->value() == "1") {
            if (!flashCommit.queueResetPrograms()) {
                sendBusy(request);
                return;
            }
            sendJsonSuccess(request);
            return;
        }
        if (request->hasParam("run", true)) {
            uint8_t id = request->getParam("run", true)->value().toInt();
            const AppPorts* p = webPorts();
            const bool started =
                p && p->control.startProgram && p->control.startProgram(p->control.ctx, id);
            if (started) {
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
        program_json::formatProgramPath(path, sizeof(path), id);
        AsyncWebServerResponse* resp = nullptr;
        const web_ram::Take take = web_ram::acquire(request, path, kContentTypeJson, false, &resp);
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
    });

    registerJsonPostBodyToTmp(server, "/program", JsonPostStreamKind::Program);

    server.on("/program", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (rejectIfFlashBusy(request)) return;
        if (!request->hasParam("id")) {
            request->send(400, kContentTypeText, "Missing id");
            return;
        }
        uint8_t id = request->getParam("id")->value().toInt();
        if (!flashCommit.queueDeleteProgram(id)) {
            sendBusy(request);
            return;
        }
        sendJsonSuccess(request);
    });

    server.on("/runtime", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("op", true)) {
            request->send(400, kContentTypeText, "Missing op");
            return;
        }
        const String& op = request->getParam("op", true)->value();
        const AppPorts* p = webPorts();
        if (op == "thermostat") {
            bool en = false;
            if (!readEnabledBodyParam(request, &en)) {
                request->send(400, kContentTypeText, "Missing enabled");
                return;
            }
            if (p && p->control.setThermostat) p->control.setThermostat(p->control.ctx, en);
            sendJsonSuccess(request);
            return;
        }
        if (op == "batterysaver") {
            bool en = false;
            if (!readEnabledBodyParam(request, &en)) {
                request->send(400, kContentTypeText, "Missing enabled");
                return;
            }
            if (p && p->control.setBatterySaver) p->control.setBatterySaver(p->control.ctx, en);
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
            const bool ok =
                p && p->control.setInputRuntime &&
                p->control.setInputRuntime(p->control.ctx, (uint16_t)id, en);
            if (!ok) {
                request->send(400, kContentTypeText, "Input id not found");
                return;
            }
            sendJsonSuccess(request);
            return;
        }
        if (op == "trigger_input_id") {
            uint32_t id = 0;
            bool en = false;
            if (!readIdEnabledBodyParams(request, &id, &en)) {
                request->send(400, kContentTypeText, "Missing parameters");
                return;
            }
            const bool ok =
                p && p->control.setInputTrigger &&
                p->control.setInputTrigger(p->control.ctx, (uint16_t)id, en);
            if (!ok) {
                request->send(400, kContentTypeText, "Input trigger id not found");
                return;
            }
            sendJsonSuccess(request);
            return;
        }
        if (op == "trigger_temp_id") {
            uint32_t id = 0;
            bool en = false;
            if (!readIdEnabledBodyParams(request, &id, &en)) {
                request->send(400, kContentTypeText, "Missing parameters");
                return;
            }
            const bool ok =
                p && p->control.setTempTrigger &&
                p->control.setTempTrigger(p->control.ctx, (uint16_t)id, en);
            if (!ok) {
                request->send(400, kContentTypeText, "Temp trigger id not found");
                return;
            }
            sendJsonSuccess(request);
            return;
        }
        request->send(400, kContentTypeText, "Invalid op");
    });
}

