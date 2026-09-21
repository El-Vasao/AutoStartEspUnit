/**
 * @file WebServer.Runtime.cpp
 * @brief Реализация runtime-логики WebServer (AP/DNS/UI lease/SSE status/log).
 *
 * Мотивация:
 * - Держим `WebServer.Core.cpp` тонким фасадом, чтобы публичный API (`include/WebServer.h`)
 *   не разрастался и не тянул реализацию по всему проекту.
 *
 * Инварианты по памяти:
 * - Не создаём “длинноживущие” heap-объекты в hot-path (особенно в SSE/статусе).
 * - Инкрементальный SSE см. `WebServer.SseIncremental.cpp` (clocks/hardware/runtime/…; полный live — GET /bootstrap).
 * - Ограничиваем очереди SSE, чтобы не раздувать lwIP буферы при медленных клиентах.
 *
 * Запрещено:
 * - Подключать внутренние заголовки web-подсистемы из других подсистем.
 * - Возвращать `String`/создавать конкатенации строк в циклах (heap churn на ESP8266).
 */
#include "web/internal/WebServerRuntime.h"

#include "web/WebServer.h"

#include "config/Config.h"
#include "common/Constants.h"
#include "common/EspHal.h"
#include "common/Logger.h"
#include "common/Utils.h"
#include "core/Core.h"
#include "fs/FSManager.h"

#include <DNSServer.h>
#include <WiFi.h>
#include <cstring>

extern DNSServer dnsServer;
extern IPAddress apIP;

namespace {
constexpr uint32_t kSseDiagTailMs = 5000UL;

uint8_t apMaxConnectionsForMode(CoreMode mode) {
    switch (mode) {
        case CoreMode::SETUP_AP:
        case CoreMode::EMERGENCY_AP:
            return APConfig::SETUP_MAX_CONNECTIONS;
        case CoreMode::NORMAL:
            return APConfig::NORMAL_MAX_CONNECTIONS;
        default:
            return APConfig::SETUP_MAX_CONNECTIONS;
    }
}
} // namespace

void WebServerRuntime::update(WebServer& ws) {
    if (ws.apActive) {
        processDNS(ws);
    }
    processUiSessions(ws);
    processSseDiagnostics(ws);

    // AP start FSM (non-blocking).
    if (ws.apStartState_ != WebServer::ApStartState::Idle) {
        const uint32_t now = millis();

        if (ws.apStartState_ == WebServer::ApStartState::WaitBefore) {
            if ((int32_t)(now - ws.apStartNotBeforeMs_) < 0) return;
            ws.apStartState_ = WebServer::ApStartState::SetMode;
        }

        if (ws.apStartState_ == WebServer::ApStartState::SetMode) {
            WiFi.mode(WIFI_AP);
            ws.apStartStageUntilMs_ = now + Delays::WEBSERVER_MODE_SWITCH_MS;
            ws.apStartState_ = WebServer::ApStartState::WaitMode;
            return;
        }

        if (ws.apStartState_ == WebServer::ApStartState::WaitMode) {
            if ((int32_t)(now - ws.apStartStageUntilMs_) < 0) return;

            apIP = WebConfig::AP_IP;
            WiFi.softAPConfig(apIP, apIP, WebConfig::AP_NETMASK);

            const char* ssid = ws.pendingApSsid_;
            const char* pass = ws.pendingApPass_;
            const uint8_t maxSta = apMaxConnectionsForMode(ws.lastStartedMode_);
            if (!WiFi.softAP(ssid, pass, APConfig::CHANNEL, APConfig::HIDDEN, maxSta)) {
                logger.log("[WebServer] WiFi.softAP() failed\n");
                ws.apStartState_ = WebServer::ApStartState::Idle;
                return;
            }

            strlcpy(ws.lastApSsid, ssid, sizeof(ws.lastApSsid));
            strlcpy(ws.lastApPass, pass, sizeof(ws.lastApPass));
            ws.apConfigKnown = true;

            ws.apStartStageUntilMs_ = now + Delays::WEBSERVER_AP_START_SETTLE_MS;
            ws.apStartState_ = WebServer::ApStartState::WaitApStart;
            return;
        }

        if (ws.apStartState_ == WebServer::ApStartState::WaitApStart) {
            if ((int32_t)(now - ws.apStartStageUntilMs_) < 0) return;

            dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
            dnsServer.start(WebConfig::DNS_PORT, "*", apIP);

            if (!ws.routesConfigured) {
                ws.setupRoutes();
                ws.routesConfigured = true;
            }

            if (!ws.serverActive) {
                ws.server.begin();
                ws.serverActive = true;
            }
            ws.apActive = true;

            char ipStr[BufferBytes::Web::IP_STRING];
            snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", apIP[0], apIP[1], apIP[2], apIP[3]);
            logger.log("[WebServer] AP started, SSID: %s, IP: %s (heap free=%u max=%u)\n",
                       ws.lastApSsid, ipStr, (unsigned)espHalFreeHeap(), (unsigned)espHalMaxBlock());

            ws.apStartState_ = WebServer::ApStartState::Idle;
            return;
        }
    }
}

void WebServerRuntime::processUiSessions(WebServer& ws) {
    const uint32_t now = millis();
    bool changed = false;
    for (auto& session : ws.uiSessions_) {
        if (!session.active) continue;
        if (now - session.lastSeenMs < WebUi::SESSION_TIMEOUT_MS) continue;
        logger.log("[WebServer] UI session expired: id=%lu, heap=%u, max=%u\n",
                   static_cast<unsigned long>(session.id),
                   (unsigned)espHalFreeHeap(),
                   (unsigned)espHalMaxBlock());
        session.active = false;
        session.id = 0;
        session.lastSeenMs = 0;
        changed = true;
    }
    if (changed) {
        const uint16_t prev = ws.activeUiSessionCount_;
        rebuildActiveUiSessionCount(ws);
        if (prev > 0 && ws.activeUiSessionCount_ == 0) {
            const size_t q = ws.events.avgPacketsWaiting();
            const uint16_t clients = refreshSseClientCount(ws);
            if (clients > 0 || q > 0) {
                logger.log("[WebServer] UI sessions=0: closing SSE (clients=%u, queue=%u)\n",
                           (unsigned)clients, (unsigned)q);
                ws.events.close();
                ws.sseClientCount = 0;
                ws.lastObservedSseClients_ = 0;
                ws.sseDiagTailUntilMs_ = 0;
            }
        }
    }

    if (ws.activeUiSessionCount_ > 0) {
        return;
    }
}

void WebServerRuntime::processDNS(WebServer& ws) {
    (void)ws;
    dnsServer.processNextRequest();
}

void WebServerRuntime::processSseDiagnostics(WebServer& ws) {
    const uint32_t now = millis();
    const uint16_t clients = refreshSseClientCount(ws);
    const size_t queue = ws.events.avgPacketsWaiting();

    if (clients > 0) {
        ws.sseDiagTailUntilMs_ = now + kSseDiagTailMs;
    } else if (ws.lastObservedSseClients_ > 0) {
        ws.sseDiagTailUntilMs_ = now + kSseDiagTailMs;
        if (queue > 0) {
            logger.log("[WebServer] SSE: clients=%u->0, queue=%u\n",
                       (unsigned)ws.lastObservedSseClients_,
                       (unsigned)queue);
        }
    }

    ws.lastObservedSseClients_ = clients;
}

void WebServerRuntime::start(WebServer& ws, CoreMode mode) {
    logger.log("[WebServer] start(CoreMode::%d)\n", (int)mode);
    ws.lastStartedMode_ = mode;
    ws.lastUiActivityMs_ = millis();

    const char* ssid = nullptr;
    const char* pass = nullptr;

    switch (mode) {
        case CoreMode::EMERGENCY_AP: {
            char unique[TextBytes::Wifi::SSID];
            generateUniqueSSID(unique, sizeof(unique));
            startWithParams(ws, unique, APConfig::DEFAULT_PASSWORD);
            return;
        }
        case CoreMode::SETUP_AP:
        case CoreMode::NORMAL:
            ssid = config.getBase().wifi.ssid;
            pass = config.getBase().wifi.password;
            break;
        default:
            logger.log("[WebServer] Unsupported mode for AP start\n");
            return;
    }

    startWithParams(ws, ssid, pass);
}

void WebServerRuntime::stop(WebServer& ws) {
    logger.log("[WebServer] stop()\n");
    ws.lastStopMs_ = millis();
    ws.apConfigKnown = false;
    ws.lastApSsid[0] = '\0';
    ws.lastApPass[0] = '\0';
    ws.sseClientCount = 0;
    ws.activeUiSessionCount_ = 0;
    ws.lastObservedSseClients_ = 0;
    ws.sseDiagTailUntilMs_ = 0;
    ws.lastSseDiagMs_ = 0;
    for (auto& session : ws.uiSessions_) {
        session = WebServer::UiSession{};
    }
    if (ws.serverActive) {
        ws.events.close();
    }
    if (ws.apActive) {
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        ws.apStartNotBeforeMs_ = ws.lastStopMs_ + Delays::WIFI_OFF_SETTLE_MS;
        ws.apActive = false;
        logger.log("[WebServer] SoftAP stopped\n");
    }
}

void WebServerRuntime::startWithParams(WebServer& ws, const char* ssid, const char* pass) {
    if (isSameAPConfig(ws, ssid, pass)) {
        logger.log("[WebServer] Already running with same config, skipping\n");
        return;
    }

    if (ws.apActive) {
        stop(ws);
    }

    if (!ssid || strlen(ssid) == 0) {
        logger.log("[WebServer] SSID is empty, using emergency AP\n");
        char emergencySSID[TextBytes::Wifi::SSID];
        generateUniqueSSID(emergencySSID, sizeof(emergencySSID));
        ssid = emergencySSID;
        pass = APConfig::DEFAULT_PASSWORD;
    }

    const size_t passLen = pass ? strlen(pass) : 0;
    const char* effectivePass = (pass && passLen >= 8 && passLen <= Limits::MAX_PASSWORD_LEN)
        ? pass
        : APConfig::DEFAULT_PASSWORD;

    logger.log("[WebServer] Scheduling AP start SSID=%s\n", ssid);

    strlcpy(ws.pendingApSsid_, ssid, sizeof(ws.pendingApSsid_));
    strlcpy(ws.pendingApPass_, effectivePass, sizeof(ws.pendingApPass_));

    const uint32_t now = millis();
    uint32_t notBefore = now;
    if (ws.lastStopMs_ != 0) {
        const uint32_t sinceStop = now - ws.lastStopMs_;
        if (sinceStop < Delays::WEBSERVER_STOP_SETTLE_MS) {
            notBefore = now + (Delays::WEBSERVER_STOP_SETTLE_MS - sinceStop);
        }
    }
    if (ws.apStartNotBeforeMs_ != 0 && (int32_t)(ws.apStartNotBeforeMs_ - notBefore) > 0) {
        notBefore = ws.apStartNotBeforeMs_;
    }
    ws.apStartNotBeforeMs_ = notBefore;
    ws.apStartState_ = WebServer::ApStartState::WaitBefore;
}

bool WebServerRuntime::sseSoftQueueAllowsSend(WebServer& ws, size_t maxAvgQueued) {
    if (ws.activeUiSessionCount_ == 0) return false;
    if (refreshSseClientCount(ws) == 0) return false;
    return ws.events.avgPacketsWaiting() < maxAvgQueued;
}

bool WebServerRuntime::sseLogChannelAllowsSend(WebServer& ws, size_t maxAvgQueued) {
    // Same gates as status path: no UI lease → no log SSE (avoids queue churn without a viewer).
    if (ws.activeUiSessionCount_ == 0) return false;
    if (refreshSseClientCount(ws) == 0) return false;
    return ws.events.avgPacketsWaiting() < maxAvgQueued;
}

bool WebServerRuntime::sseActiveUiOk(const WebServer& ws) {
    return ws.activeUiSessionCount_ != 0;
}

bool WebServerRuntime::sseQueueBackpressureAtLeast(WebServer& ws, size_t avgPacketsWaitingThreshold) {
    return ws.events.avgPacketsWaiting() >= avgPacketsWaitingThreshold;
}

void WebServerRuntime::sseSendEvent(WebServer& ws, const char* payload, const char* sseName, uint32_t ms) {
    ws.events.send(payload, sseName, ms);
}

void WebServerRuntime::broadcastLog(WebServer& ws, const char* message) {
    if (!message) return;
    static char s_lastLog[Logging::MAX_MESSAGE_LENGTH];
    static bool s_haveLast;
    if (s_haveLast && strcmp(s_lastLog, message) == 0) return;
    strlcpy(s_lastLog, message, sizeof s_lastLog);
    s_haveLast = true;
    if (!sseLogChannelAllowsSend(ws, WebSseLimits::SSE_SOFT_QUEUE_MAX)) return;
    ws.events.send(message, "log", millis());
}

void WebServerRuntime::broadcastStatus(WebServer& ws) {
    tickSseIncremental(ws, millis());
}

uint16_t WebServerRuntime::refreshSseClientCount(WebServer& ws) {
    ws.sseClientCount = static_cast<uint16_t>(ws.events.count());
    return ws.sseClientCount;
}

bool WebServerRuntime::touchUiSession(WebServer& ws, uint32_t id, uint32_t now) {
    if (id == 0) return false;
    ws.lastUiActivityMs_ = now;

    int freeIdx = -1;
    int oldestIdx = -1;
    uint32_t oldestAge = 0;

    for (unsigned i = 0; i < WebUi::MAX_UI_SESSIONS; i++) {
        WebServer::UiSession& session = ws.uiSessions_[i];
        if (session.active && session.id == id) {
            session.lastSeenMs = now;
            return true;
        }
        if (!session.active && freeIdx < 0) {
            freeIdx = static_cast<int>(i);
        }
        const uint32_t age = now - session.lastSeenMs;
        if (oldestIdx < 0 || age > oldestAge) {
            oldestIdx = static_cast<int>(i);
            oldestAge = age;
        }
    }

    const int idx = (freeIdx >= 0) ? freeIdx : oldestIdx;
    if (idx < 0) return false;

    WebServer::UiSession& slot = ws.uiSessions_[idx];
    const bool evictingActive = slot.active && slot.id != 0 && slot.id != id;
    if (evictingActive) {
        logger.log("[WebServer] UI session evicted: %lu -> %lu\n",
                   static_cast<unsigned long>(slot.id), static_cast<unsigned long>(id));
    }
    slot.id = id;
    slot.lastSeenMs = now;
    slot.active = true;
    ws.lastUiActivityMs_ = now;
    rebuildActiveUiSessionCount(ws);
    return true;
}

bool WebServerRuntime::closeUiSession(WebServer& ws, uint32_t id) {
    if (id == 0) return false;
    for (auto& session : ws.uiSessions_) {
        if (!session.active || session.id != id) continue;
        session.active = false;
        session.id = 0;
        session.lastSeenMs = 0;
        rebuildActiveUiSessionCount(ws);
        return true;
    }
    return false;
}

void WebServerRuntime::rebuildActiveUiSessionCount(WebServer& ws) {
    uint16_t count = 0;
    for (const auto& session : ws.uiSessions_) {
        if (session.active) count++;
    }
    ws.activeUiSessionCount_ = count;
}

bool WebServerRuntime::isSameAPConfig(const WebServer& ws, const char* ssid, const char* pass) {
    if (!ws.apActive || !ws.serverActive || !ws.apConfigKnown) return false;
    if (!ssid || !pass) return false;
    return strcmp(ws.lastApSsid, ssid) == 0 && strcmp(ws.lastApPass, pass) == 0;
}

void WebServerRuntime::dumpAPConfig(const char* ssid, const char* pass, const char* context) {
    logger.log("[WebServer] %s: SSID=\"%s\", PASS=\"%s\" Mode: %d\n",
               context, ssid ? ssid : "NULL", pass ? pass : "NULL", WiFi.getMode());
}

