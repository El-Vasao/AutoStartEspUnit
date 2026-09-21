// src/core/Core.CellularCore.cpp
#include "core/internal/CellularCore.h"

#include "gsm/GSMController.h"
#include "mqtt/MQTTClient.h"
#include "web/WebServer.h"
#include "common/Logger.h"
#include "core/Core.h"

/**
 * @file Core.CellularCore.cpp
 * @brief Реализация glue‑логики сотового канала (GSM + MQTT) для Core.
 *
 * Принципы:
 * - Неблокирующее обслуживание: GSM всегда тикает в `service()`, MQTT — когда модем READY.
 * - В NORMAL: SoftAP up → service; SoftAP down / OTA pressure → suspend (см. Core.Modes).
 * - MQTT reconnect при service остаётся enabled (WDT — в transport budgets).
 *
 * Память:
 * - Без аллокаций heap (только указатели на уже существующие подсистемы).
 */

void CellularCore::init(GSMController& gsm, MQTTClient& mqtt, WebServer& web) {
    _gsm = &gsm;
    _mqtt = &mqtt;
    _web = &web;
    _lastReattachRequestMs = 0;
    _lastLoggedFailStreak = 0;
}

void CellularCore::service() {
    if (!_gsm || !_mqtt || !_web) return;

    if (!_gsmStarted) {
        _gsm->begin();
        _gsmStarted = true;
    }

    _gsm->update();
    if (_gsm->isReady()) {
        // Policy: MQTT reconnect is always enabled (even with active UI/SSE).
        // WDT-safety is enforced at transport layer (pump + budgets) for SIM800.
        _mqtt->setReconnectEnabled(true);

        if (!_mqttStarted) {
            _mqtt->begin();
            _mqttStarted = true;
        }
        if (_mqttStarted) {
            _mqtt->loop();
        }

        const uint8_t failStreak = _mqtt->getConsecutiveConnectFails();
        if (failStreak >= 3) {
            const uint32_t now = millis();
            const bool canRequest = (_lastReattachRequestMs == 0) || (now - _lastReattachRequestMs >= 5000UL);
            if (canRequest) {
                _lastReattachRequestMs = now;
                _gsm->requestReattach();
            }
            if (failStreak != _lastLoggedFailStreak) {
                _lastLoggedFailStreak = failStreak;
                logger.log("[Core] MQTT connect fails=%u, requesting GSM reattach%s\n", (unsigned)failStreak,
                           canRequest ? "" : " (cooldown)");
                core.logHeapSnapshot("mqtt_connect_fail");
            }
        } else {
            _lastLoggedFailStreak = failStreak;
        }
    } else if (_mqttStarted) {
        _mqtt->disconnect();
        _mqttStarted = false;
        _lastLoggedFailStreak = 0;
    }
}

void CellularCore::suspend() {
    if (!_gsm || !_mqtt || !_web) return;
    if (_mqttStarted) {
        _mqtt->disconnect();
        _mqttStarted = false;
    }
    if (_gsmStarted) {
        _gsm->stop();
        _gsmStarted = false;
    }
    _lastReattachRequestMs = 0;
    _lastLoggedFailStreak = 0;
}

