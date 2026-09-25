#pragma once

#include <Arduino.h>

#include "common/Constants.h"

class GSMController;
class MQTTClient;
class WebServer;
class TimeSyncManager;

/**
 * @file CellularCore.h
 * @brief Внутренний “mini-core” для жизненного цикла сотового канала (GSM + MQTT), используемый Core.
 *
 * Архитектура:
 * - Это часть реализации core‑подсистемы (не публичный API).
 * - Содержит только glue‑логику: когда запускать/останавливать GSM и MQTT, и какие ограничения применять.
 *
 * Память/устойчивость (ESP8266):
 * - Без heap, без виртуальных методов, без динамических буферов.
 * - Все зависимости передаются через `init()` (указатели), чтобы избежать проблем порядка статической инициализации.
 *
 * Запрещено:
 * - Подключать этот заголовок вне core‑подсистемы.
 * - Добавлять сюда бизнес-логику программ/триггеров/файловой системы.
 */
class CellularCore {
public:
    CellularCore() = default;
    void init(GSMController& gsm, MQTTClient& mqtt, WebServer& web, TimeSyncManager& timeSync);

    void service();
    void suspend();

private:
    void drainMqttDisconnect_();

    GSMController* _gsm{nullptr};
    MQTTClient* _mqtt{nullptr};
    WebServer* _web{nullptr};
    TimeSyncManager* _timeSync{nullptr};
    bool _gsmStarted{false};
    bool _mqttStarted{false};
    /// One-shot settle after power-on / first service before gsm.begin().
    bool _bootSettlePending{true};
    uint32_t _bootSettleUntilMs{0};
    uint32_t _lastSettleLogMs{0};
    uint32_t _lastReattachRequestMs{0};
    uint8_t _lastLoggedFailStreak{0};
    bool _loggedBootNtpWait{false};
    /// Latched after MQTT Connected with failStreak==0 → gsm.clearReattachBackoff().
    bool _mqttStableNoted{false};
};
