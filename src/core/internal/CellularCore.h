#pragma once

#include <Arduino.h>

#include "common/Constants.h"

class GsmController;
class MqttClient;
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
 * Память/устойчивость:
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
    void init(GsmController& gsm, MqttClient& mqtt, WebServer& web, TimeSyncManager& timeSync);

    void service();
    void suspend();

private:
    /// Слить отключение MQTT и простой CIP. false, если ещё занято (повтор на следующем service).
    bool drainMqttDisconnect_();

    GsmController* _gsm{nullptr};
    MqttClient* _mqtt{nullptr};
    WebServer* _web{nullptr};
    TimeSyncManager* _timeSync{nullptr};
    bool _gsmStarted{false};
    bool _mqttStarted{false};
    /// Одноразовая пауза settle после включения питания / первого service до gsm.begin().
    bool _bootSettlePending{true};
    uint32_t _bootSettleUntilMs{0};
    uint32_t _lastSettleLogMs{0};
    uint32_t _lastReattachRequestMs{0};
    uint8_t _lastLoggedFailStreak{0};
    bool _loggedBootNtpWait{false};
    /// Один лог на эпоху слива MQTT (voice/SMS или time-sync), без спама каждый service().
    bool _loggedMqttFlush{false};
    /// Защёлка после MQTT Connected при failStreak==0 → gsm.clearReattachBackoff().
    bool _mqttStableNoted{false};
};
