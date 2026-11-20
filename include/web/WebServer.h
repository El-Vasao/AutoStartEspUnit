/**
 * @file WebServer.h
 * @brief Публичный API web-подсистемы (HTTP + SoftAP + SSE).
 *
 * Важно для архитектуры:
 * - Этот заголовок — единственная точка входа для остальных подсистем.
 * - Внутренняя реализация должна оставаться в `src/web/` (и подпапках) и не “просачиваться” наружу.
 *
 * Важно для памяти (ESP32-C3 SoftAP + SSE):
 * - Подсистема web чувствительна к heap/очередям lwIP. Любые изменения должны учитывать
 *   ограничения `WebSseLimits::*` и избегать `String`-чёрна в горячих путях.
 *
 * Разрастание компиляции:
 * - Публичный заголовок пока включает `ESPAsyncWebServer.h` / `WiFi.h`, потому что
 *   `AsyncWebServer` и `AsyncEventSource` — члены класса. Тонкий заголовок (pimpl только для runtime)
 *   — отдельная отложенная работа; не блокирует остальные работы P1.
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
struct AppPorts;

/**
 * @brief Веб-сервер для управления устройством.
 * Запускает точку доступа, обрабатывает HTTP-запросы и управляет SSE-соединениями.
 *
 * Важно:
 * - Сервер предполагается “локальным” (AP, перехватывающий портал). Сейчас нет авторизации.
 * - SSE используется как транспорт для UI (лог + статус).
 * - Тяжёлые ответы (JSON) компактные: фиксированные буферы, без `String` на горячем пути.
 * - В NORMAL веб не считается опциональным модулем — см. блок «Продукт» выше.
 */
class WebServer {
public:
    WebServer();

    /// Подключается в Core::begin (корень композиции). Изменяющий API идёт через AppPorts, без глобальных геттеров core.
    void setAppPorts(const AppPorts* ports) { ports_ = ports; }
    const AppPorts* appPorts() const { return ports_; }

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

    // Отправка лога всем SSE-клиентам
    void broadcastLog(const char* message);

    /// Одна итерация инкрементального SSE (clocks/hardware/runtime/…, дифф по хешам; полный live — GET /bootstrap).
    void tickSseIncremental(uint32_t nowMs);

    // Отправка полного статуса всем SSE-клиентам
    void broadcastStatus();
    // Отправка статуса с повышенным приоритетом (например, смена режима/OTA).
    void broadcastStatusForce();

    // Обновить и вернуть фактическое число SSE-клиентов.
    uint16_t refreshSseClientCount();

    uint16_t activeUiSessionCount() const { return activeUiSessionCount_; }

    /// Метка времени (millis) последней замеченной активности UI (heartbeat/касание).
    /// Нужна логике таймаута AP в режиме NORMAL.
    uint32_t lastUiActivityMs() const { return lastUiActivityMs_; }

    /**
     * @brief true, если любая операция flash ещё не завершена.
     *
     * Контракт (LittleFS):
     * - когда возвращает true, любые новые операции записи/удаления должны быть отвергнуты/отложены;
     * - сюда входят не только глобальные FS-deferred операции, но и “внутренние” web POST-потоки
     *   (JSON в tmp, ожидающий применения в `flashCommit.tick`).
     *
     * Архитектура:
     * - метод намеренно реализован в `.cpp`, чтобы публичный заголовок web-подсистемы
     *   не тянул `FsManager.h` (снижаем связанность и разрастание компиляции).
     */
    bool isFlashBusy() const;

    /// OtaHandler: ожидание `final` превысило лимит — следующий chunk upload должен корректно сорваться.
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

    // ---------- Отложенные операции flash и статус фиксации (для UI) ----------
    /// Идёт приём POST JSON конфигурации (поток в tmp) — не пересекать с другими операциями Flash
    bool postedConfigJsonPending;
    /// Идёт приём POST JSON программы (поток в tmp)
    bool postedProgramJsonPending;

    AsyncEventSource events;  ///< встроенный обработчик событий SSE (путь "/events")

    const AppPorts* ports_{nullptr};

    // Запуск с конкретными SSID и паролем
    void startWithParams(const char* ssid, const char* pass);

    // Регистрация всех маршрутов
    void setupRoutes();

    // Проверка, совпадает ли текущая конфигурация AP с заданной
    bool isSameAPConfig(const char* ssid, const char* pass) const;

    void rebuildActiveUiSessionCount();

    /// POST JSON → tmp (FsManager), затем deferred apply в `flashCommit.tick`.
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

