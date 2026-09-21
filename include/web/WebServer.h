/**
 * @file WebServer.h
 * @brief Публичный API web-подсистемы (HTTP + SoftAP + SSE).
 *
 * Важно для архитектуры:
 * - Этот заголовок — единственная точка входа для остальных подсистем.
 * - Внутренняя реализация должна оставаться в `src/web/` (и подпапках) и не “просачиваться” наружу.
 *
 * Важно для памяти (ESP8266):
 * - Подсистема web чувствительна к heap/очередям lwIP. Любые изменения должны учитывать
 *   ограничения `WebSseLimits::*` и избегать `String`-чёрна в горячих путях.
 *
 * Продукт (режим NORMAL):
 * - В `CoreMode::NORMAL` веб-сервер и доступный пользователю UI (HTTP + SSE) считаются **всегда включёнными**;
 *   оптимизации RAM не должны опираться на сценарий «выключить веб в NORMAL». Узкие места снимать буферами
 *   (например лимит очереди SSE в ESPAsyncWebServer), сериализацией JSON в фиксированные буферы и т.п.,
 *   а не отказом от постоянного веба в штатной работе.
 *
 * Запрещено:
 * - Подключать внутренние заголовки web-подсистемы из других подсистем.
 */
// include/web/WebServer.h
#pragma once

#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include "common/Constants.h"

enum class CoreMode : uint8_t;
class WebServerRuntime;

/**
 * @brief Веб-сервер для управления устройством.
 * Запускает точку доступа, обрабатывает HTTP-запросы и управляет SSE-соединениями.
 *
 * Важно:
 * - Сервер предполагается “локальным” (AP/captive portal). Сейчас нет авторизации.
 * - SSE используется как транспорт для UI (лог + статус).
 * - Тяжёлые ответы (JSON) желательно делать компактными: ESP8266 чувствителен к фрагментации heap.
 * - В NORMAL веб не считается опциональным модулем — см. блок «Продукт» выше.
 */
class WebServer {
public:
    WebServer();

    // Единый update для web-части (DNS+lease+SSE housekeeping).
    void update();

    // Запуск сервера в указанном режиме
    void start(CoreMode mode);

    // Полная остановка AP и сервера
    void stop();

    // Обработка DNS-запросов (для Captive Portal)
    void processDNS();

    // Обработка lease/heartbeat UI-сессий.
    void processUiSessions();

    // Фоновая диагностика SSE (подключения, хвост очереди, память).
    void processSseDiagnostics();

    // Активен ли сервер (AP + HTTP)
    bool isActive() const;

    // IP-адрес точки доступа
    IPAddress getAPIP() const;

    // Отправка лога всем SSE-клиентам
    void broadcastLog(const char* message);

    /// Одна итерация инкрементального SSE (clocks/hardware/runtime/…, дифф по хешам; полный live — GET /bootstrap).
    void tickSseIncremental(uint32_t nowMs);

    // Отправка полного статуса всем SSE-клиентам
    void broadcastStatus();
    // Отправка статуса с повышенным приоритетом (например, смена режима/OTA).
    void broadcastStatusForce();

    /// Закрыть SSE `/events` и сбросить UI-сессии; HTTP остаётся (OTA upload не ломаем).
    void closeSseForOta();

    // Обновить и вернуть фактическое число SSE-клиентов.
    uint16_t refreshSseClientCount();

    uint16_t activeUiSessionCount() const { return activeUiSessionCount_; }

    /// Timestamp (millis) of last observed UI activity (heartbeat/touch).
    /// Used for AP timeout logic in NORMAL mode.
    uint32_t lastUiActivityMs() const { return lastUiActivityMs_; }

    /**
     * @brief True when any flash operation is pending.
     *
     * Контракт (важно для ESP8266/LittleFS):
     * - когда возвращает true, любые новые операции записи/удаления должны быть отвергнуты/отложены;
     * - сюда входят не только глобальные FS-deferred операции, но и “внутренние” web POST-потоки
     *   (JSON в tmp, ожидающий применения в `processDeferred()`).
     *
     * Архитектура:
     * - метод намеренно реализован в `.cpp`, чтобы публичный заголовок web-подсистемы
     *   не тянул `FSManager.h` (снижаем связанность и compile fanout).
     */
    bool isFlashBusy() const;

    /// OTAHandler: ожидание `final` превысило лимит — следующий chunk upload должен корректно сорваться.
    void markOtaHttpUploadAwaitTimedOut();

    bool touchUiSession(uint32_t id, uint32_t now);
    bool closeUiSession(uint32_t id);

private:
    friend class WebServerRuntime;

    enum class ApStartState : uint8_t {
        Idle = 0,
        WaitBefore,
        SetMode,
        WaitMode,
        WaitApStart
    };

    struct UiSession {
        uint32_t id{0};
        uint32_t lastSeenMs{0};
        bool active{false};
    };

    uint16_t sseClientCount;  ///< количество подключённых SSE клиентов
    uint16_t activeUiSessionCount_{0};
    uint32_t lastSseDiagMs_{0};
    uint16_t lastObservedSseClients_{0};
    uint32_t sseDiagTailUntilMs_{0};
    uint32_t lastUiActivityMs_{0};
    UiSession uiSessions_[WebUi::MAX_UI_SESSIONS]{};
    CoreMode lastStartedMode_{CoreMode::NORMAL};
    uint32_t lastStopMs_{0};
    ApStartState apStartState_{ApStartState::Idle};
    uint32_t apStartNotBeforeMs_{0};
    uint32_t apStartStageUntilMs_{0};
    char pendingApSsid_[TextBytes::Wifi::SSID]{};
    char pendingApPass_[TextBytes::Wifi::PASSWORD]{};

    AsyncWebServer server;   ///< объект асинхронного сервера
    bool apActive;            ///< активна ли точка доступа
    bool serverActive;        ///< запущен ли HTTP-сервер
    bool routesConfigured;    ///< маршруты зарегистрированы (нельзя регистрировать повторно из-за OOM)

    /// Последние параметры SoftAP (без String при сравнении в isSameAPConfig)
    char lastApSsid[TextBytes::Wifi::SSID];
    char lastApPass[TextBytes::Wifi::PASSWORD];
    bool apConfigKnown;

    // ---------- Deferred flash ops + commit status (for UI) ----------
    /// Идёт приём POST JSON конфигурации (поток в tmp) — не пересекать с другими операциями Flash
    bool postedConfigJsonPending;
    /// Идёт приём POST JSON программы (поток в tmp)
    bool postedProgramJsonPending;

    AsyncEventSource events;  ///< встроенный обработчик событий SSE (путь "/events")

    /// Unified status sender with queue cap.
    void sendStatus_(size_t maxQueueDepth);

    // Запуск с конкретными SSID и паролем
    void startWithParams(const char* ssid, const char* pass);

    // Регистрация всех маршрутов
    void setupRoutes();

    // Проверка, совпадает ли текущая конфигурация AP с заданной
    bool isSameAPConfig(const char* ssid, const char* pass) const;

    // Отладка: вывод текущей конфигурации AP
    void dumpAPConfig(const char* ssid, const char* pass, const char* context) const;
    void rebuildActiveUiSessionCount();

    /// POST JSON → tmp (FSManager), затем deferred apply в `processDeferred()`.
    enum class JsonPostStreamKind : uint8_t { Config, Program };
    static const char* jsonPostTmpPath(JsonPostStreamKind k);
    static bool jsonPostOtherSidePending(JsonPostStreamKind k);
    static void jsonPostSetThisPending(JsonPostStreamKind k, bool v);
    static void jsonPostSetDeferred(JsonPostStreamKind k);
    static void jsonPostStreamOnBody(JsonPostStreamKind k, AsyncWebServerRequest* request, uint8_t* data,
                                     size_t len, size_t index, size_t total);
    static void jsonPostStreamOnRequest(JsonPostStreamKind k, AsyncWebServerRequest* request);
    static void registerJsonPostBodyToTmp(AsyncWebServer& srv, const char* path, JsonPostStreamKind kind);

    // Регистрация групп маршрутов (рефакторинг setupRoutes()).
    void setupSseEndpointRoutes_();
    void setupRootRoutes_();
    void setupApiRoutes_();
    void setupCaptivePortalRoutes_();
};

extern WebServer webServer;

// SSE: обновление статуса (JSON) — вызывается из других модулей.
// Текстовые логи в UI — только через `logger.log()` (см. Logger.h); прямой вызов
// рассылки строк в SSE из приложения не предусмотрен.
void sseBroadcastStatus();

