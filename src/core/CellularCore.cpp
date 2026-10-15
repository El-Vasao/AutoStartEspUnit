// src/core/Core.CellularCore.cpp
#include "core/internal/CellularCore.h"

#include "gsm/GSMController.h"
#include "mqtt/MQTTClient.h"
#include "web/WebServer.h"
#include "core/TimeSyncManager.h"
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
 * - First MQTT begin waits for boot time cascade (CCLK/CIPSHUT/CIPGSMLOC/CNTP before CIP).
 * - CCLK-lite probes may run with MQTT up; full cascade drains MQTT (same recipe as voice).
 * - Leave READY drains MQTT (same as suspend) before bearer AT continues.
 * - Cellular suspend только в SETUP / EMERGENCY / OTA (см. Core.Modes); SoftAP в NORMAL
 *   сосуществует с GSM/MQTT.
 */

void CellularCore::init(GSMController& gsm, MQTTClient& mqtt, WebServer& web, TimeSyncManager& timeSync) {
    _gsm = &gsm;
    _mqtt = &mqtt;
    _web = &web;
    _timeSync = &timeSync;
    _lastReattachRequestMs = 0;
    _lastLoggedFailStreak = 0;
    _bootSettlePending = true;
    _bootSettleUntilMs = 0;
    _lastSettleLogMs = 0;
    _loggedBootNtpWait = false;
    _mqttStableNoted = false;
    // MQTT Ctrl/CIPSTART: TCP epoch, heavy time cascade, CCLK-lite (mutex vs CIPSEND), or voice.
    _mqtt->setCtrlPlaneBusy(
        [](void* ctx) -> bool {
            auto* g = static_cast<GSMController*>(ctx);
            return g->tcpEpochBusy() || g->timeSyncBusy() || g->modemServiceEpochBusy();
        },
        _gsm);
    // rx_incomplete forensics: transport +IPD/send state + SoftAP activity.
    _mqtt->setOnRxIncomplete(
        [](void* ctx, const uint8_t* /*head*/, uint16_t have, uint32_t need) {
            auto* self = static_cast<CellularCore*>(ctx);
            if (!self || !self->_gsm) return;
            self->_gsm->logTcpRxForensic("rx_incomplete");
            const bool ap = self->_web && self->_web->isActive();
            const uint16_t ui = self->_web ? self->_web->activeUiSessionCount() : 0;
            logger.log("[Cellular] rx_incomplete softap=%u ui_sessions=%u have=%u need=%u\n",
                       (unsigned)ap, (unsigned)ui, (unsigned)have, (unsigned)need);
        },
        this);
}

bool CellularCore::drainMqttDisconnect_() {
    if (!_gsm || !_mqtt) return true;
    _mqtt->setReconnectEnabled(false);
    if (_mqttStarted || _mqtt->needsDisconnectDrain() || _mqtt->isSessionConnected()) {
        _mqtt->disconnect();
    }
    for (uint8_t i = 0; i < Cellular::MQTT_DRAIN_MAX_TICKS; ++i) {
        _gsm->update();
        _mqtt->loop();
        if (!_mqtt->needsDisconnectDrain() && !_gsm->tcpBusBusy()) {
            _mqttStarted = false;
            _mqttStableNoted = false;
            _lastLoggedFailStreak = 0;
            return true;
        }
        yield();
    }
    // Incomplete — keep draining next service(); do not arm voice/time AT yet.
    return false;
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
            if (_lastSettleLogMs == 0 || (now - _lastSettleLogMs) >= Cellular::SETTLE_LOG_INTERVAL_MS) {
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

    // Exclusive voice/SMS epoch: drain MQTT before ATD/ATA/CMGS.
    if (_gsm->serviceEpochNeedsMqttDrain()) {
        if (_mqttStarted || (_mqtt && _mqtt->needsDisconnectDrain())) {
            if (_mqttStarted) {
                logger.log("[Cellular] draining MQTT for voice/SMS epoch\n");
            }
            if (!drainMqttDisconnect_()) return;
        } else if (_mqtt) {
            _mqtt->setReconnectEnabled(false);
        }
        _gsm->notifyMqttDrainedForServiceEpoch();
    }

    // Heavy time-sync yield: drain MQTT so CCLK→CIPSHUT→CIPGSMLOC→CNTP can run.
    // CCLK-lite does not request drain. Skip while voice owns the modem (mutual exclude).
    if (_timeSync && _timeSync->needsMqttDrainForSync() && !_gsm->modemServiceEpochBusy() &&
        !_gsm->serviceEpochNeedsMqttDrain()) {
        if (_mqttStarted || (_mqtt && _mqtt->needsDisconnectDrain())) {
            if (_mqttStarted) {
                logger.log("[Cellular] draining MQTT for time-sync yield\n");
            }
            if (!drainMqttDisconnect_()) return;
        } else if (_mqtt) {
            _mqtt->setReconnectEnabled(false);
        }
        _timeSync->notifyMqttDrainedForSync();
    }

    if (_gsm->isReady()) {
        if (_gsm->modemServiceEpochBusy()) {
            // Keep MQTT down for the whole voice/SMS epoch.
            return;
        }

        if (_timeSync && _timeSync->isSyncYieldEpochBusy()) {
            // Keep MQTT down for drain + cascade (boot-like full time sync).
            _mqtt->setReconnectEnabled(false);
            return;
        }

        // Policy: MQTT reconnect is always enabled (even with active UI/SSE).
        // WDT-safety is enforced at transport layer (pump + budgets) for SIM800.
        _mqtt->setReconnectEnabled(true);

        // Serialize boot: time cascade before first MQTT CIPSTART.
        if (_timeSync && !_timeSync->isBootTimeSettled()) {
            if (!_loggedBootNtpWait) {
                _loggedBootNtpWait = true;
                logger.log("[Cellular] waiting boot time before MQTT\n");
            }
            return;
        }
        if (_loggedBootNtpWait) {
            _loggedBootNtpWait = false;
            logger.log("[Cellular] boot time settled, starting MQTT\n");
        }

        if (!_mqttStarted) {
            _mqtt->begin();
            _mqttStarted = true;
        }
        if (_mqttStarted) {
            if (_gsm->takeTcpRxOverflow()) {
                _mqtt->onTransportRxOverflow();
            }
            // Always tick MQTT so the RX ring drains during CIPSEND; writes no-op while bus locked.
            _mqtt->loop();
        }

        // After a live MQTT session, clear GSM reattach backoff (not on every READY entry).
        if (_mqtt->isSessionConnected() && _mqtt->getConsecutiveConnectFails() == 0) {
            if (!_mqttStableNoted) {
                _gsm->clearReattachBackoff();
                _mqttStableNoted = true;
            }
        } else {
            _mqttStableNoted = false;
        }

        const uint8_t failStreak = _mqtt->getConsecutiveConnectFails();
        if (failStreak >= Cellular::MQTT_FAIL_STREAK_REATTACH) {
            const uint32_t now = millis();
            const bool canRequest = (_lastReattachRequestMs == 0) ||
                (now - _lastReattachRequestMs >= Cellular::MQTT_REATTACH_REQUEST_COOLDOWN_MS);
            if (canRequest && !_gsm->tcpBusBusy() && !_gsm->timeSyncBusy() &&
                !_gsm->modemServiceEpochBusy()) {
                _lastReattachRequestMs = now;
                _gsm->requestReattach();
            }
            if (failStreak != _lastLoggedFailStreak) {
                _lastLoggedFailStreak = failStreak;
                const char* why = _mqtt->getLastConnectFailReason();
                logger.log("[Cellular] MQTT connect fails=%u reason=%s, requesting GSM reattach%s\n",
                           (unsigned)failStreak, (why && why[0]) ? why : "?",
                           canRequest ? "" : " (cooldown)");
                core.logHeapSnapshot("mqtt_connect_fail");
            }
        } else {
            _lastLoggedFailStreak = failStreak;
        }
    } else if (_mqttStarted || (_mqtt && _mqtt->needsDisconnectDrain())) {
        // Leave READY (reattach/ERROR): drain offline+DISCONNECT like suspend (C1).
        logger.log("[Cellular] GSM left READY — draining MQTT\n");
        (void)drainMqttDisconnect_();
    }
}

void CellularCore::suspend() {
    if (!_gsm || !_mqtt || !_web) return;
    if (!_mqttStarted && !_gsmStarted) return;
    if (_mqttStarted || _mqtt->needsDisconnectDrain()) {
        (void)drainMqttDisconnect_();
    }
    if (_gsmStarted) {
        _gsm->stop();
        _gsmStarted = false;
    }
    // Keep _bootSettlePending false — only cold first start waits full settle.
    _lastReattachRequestMs = 0;
    _lastLoggedFailStreak = 0;
    _loggedBootNtpWait = false;
    _mqttStableNoted = false;
}
