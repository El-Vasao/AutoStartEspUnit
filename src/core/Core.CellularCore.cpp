// src/core/Core.CellularCore.cpp
#include "core/internal/CellularCore.h"

#include "gsm/GSMController.h"
#include "mqtt/MQTTClient.h"
#include "web/WebServer.h"
#include "common/Logger.h"
#include "common/Constants.h"
#include "core/Core.h"

/**
 * @file Core.CellularCore.cpp
 * @brief Реализация glue‑логики сотового канала (GSM + MQTT) для Core.
 *
 * Принципы:
 * - После бута — settle `GSM::POST_BOOT_SETTLE_MS` до первого `gsm.begin()`.
 * - Неблокирующее обслуживание: GSM тикает в `service()`, MQTT — когда модем READY.
 * - В NORMAL: SoftAP up → service; SoftAP down / OTA pressure → suspend (см. Core.Modes).
 */

void CellularCore::init(GSMController& gsm, MQTTClient& mqtt, WebServer& web) {
    _gsm = &gsm;
    _mqtt = &mqtt;
    _web = &web;
    _lastReattachRequestMs = 0;
    _lastLoggedFailStreak = 0;
    _bootSettlePending = true;
    _bootSettleUntilMs = 0;
    _lastSettleLogMs = 0;
}

void CellularCore::service() {
    if (!_gsm || !_mqtt || !_web) return;

    if (_bootSettlePending) {
        const uint32_t now = millis();
        if (_bootSettleUntilMs == 0) {
            _bootSettleUntilMs = now + GSM::POST_BOOT_SETTLE_MS;
            logger.log("[Cellular] GSM post-boot settle %u ms\n", (unsigned)GSM::POST_BOOT_SETTLE_MS);
        }
        if ((int32_t)(now - _bootSettleUntilMs) < 0) {
            if (_lastSettleLogMs == 0 || (now - _lastSettleLogMs) >= 5000UL) {
                _lastSettleLogMs = now;
                const uint32_t left = _bootSettleUntilMs - now;
                logger.log("[Cellular] GSM settle remaining ~%u ms\n", (unsigned)left);
            }
            return;
        }
        _bootSettlePending = false;
        logger.log("[Cellular] GSM settle done, starting modem\n");
    }

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
            // Avoid hammering MQTT while modem TCP is mid-CIPSEND / connecting.
            if (!_gsm->tcpBusBusy()) {
                _mqtt->loop();
            }
        }

        const uint8_t failStreak = _mqtt->getConsecutiveConnectFails();
        if (failStreak >= 3) {
            const uint32_t now = millis();
            const bool canRequest = (_lastReattachRequestMs == 0) || (now - _lastReattachRequestMs >= 5000UL);
            if (canRequest && !_gsm->tcpBusBusy()) {
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
    // Keep _bootSettlePending false — only cold first start waits full settle.
    _lastReattachRequestMs = 0;
    _lastLoggedFailStreak = 0;
}
