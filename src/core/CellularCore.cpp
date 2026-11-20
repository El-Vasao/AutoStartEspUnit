// src/core/CellularCore.cpp
#include "core/internal/CellularCore.h"

#include "gsm/GsmController.h"
#include "mqtt/MqttClient.h"
#include "web/WebServer.h"
#include "core/TimeSyncManager.h"
#include "common/Logger.h"
#include "common/Constants.h"
#include "core/Core.h"

/**
 * @file CellularCore.cpp
 * @brief Реализация glue‑логики сотового канала (GSM + MQTT) для Core.
 *
 * Принципы:
 * - После бута — settle `Gsm::POST_BOOT_SETTLE_MS` до первого `gsm.begin()`.
 * - Неблокирующее обслуживание: GSM тикает в `service()`, MQTT — когда модем READY.
 * - Первый MQTT begin ждёт каскад времени на старте (CCLK/CIPSHUT/CIPGSMLOC/CNTP до CIP).
 * - Пробы CCLK-lite могут идти при поднятом MQTT; полный каскад сливает MQTT (тот же приём, что у голоса).
 * - Выход из READY сливает MQTT (как suspend), прежде чем AT bearer продолжится.
 * - Cellular suspend только в SETUP / EMERGENCY / OTA (см. Core.Modes); SoftAP в NORMAL
 *   сосуществует с GSM/MQTT.
 */

void CellularCore::init(GsmController& gsm, MqttClient& mqtt, WebServer& web, TimeSyncManager& timeSync) {
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
    // MQTT Ctrl/CIPSTART: эпоха TCP, тяжёлый каскад времени, CCLK-lite (взаимоисключение с CIPSEND) или голос.
    _mqtt->setCtrlPlaneBusy(
        [](void* ctx) -> bool {
            auto* g = static_cast<GsmController*>(ctx);
            return g->tcpEpochBusy() || g->timeSyncBusy() || g->modemServiceEpochBusy();
        },
        _gsm);
    _mqtt->setTransportSendBusy(
        [](void* ctx) -> bool {
            auto* g = static_cast<GsmController*>(ctx);
            return g->tcpSendBusy();
        },
        _gsm);
    // Разбор rx_incomplete: состояние транспорта +IPD/send и активность SoftAP.
    _mqtt->setOnRxIncomplete(
        [](void* ctx, const uint8_t* /*head*/, uint16_t have, uint32_t need) {
            auto* self = static_cast<CellularCore*>(ctx);
            if (!self || !self->_gsm) return;
            self->_gsm->logTcpRxForensic("rx_incomplete");
            const bool ap = self->_web && self->_web->isActive();
            const uint16_t ui = self->_web ? self->_web->activeUiSessionCount() : 0;
            logger.log(LogModule::CellularCore, "rx_incomplete softap=%u ui_sessions=%u have=%u need=%u\n",
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
    const uint32_t t0 = millis();
    for (uint8_t i = 0; i < Cellular::MQTT_DRAIN_MAX_TICKS; ++i) {
        _gsm->update();
        _mqtt->loop();
        if (!_mqtt->needsDisconnectDrain() && !_gsm->tcpBusBusy()) {
            _mqttStarted = false;
            _mqttStableNoted = false;
            _lastLoggedFailStreak = 0;
            return true;
        }
        if ((uint32_t)(millis() - t0) >= Cellular::MQTT_DRAIN_MAX_MS) break;
        yield();
    }
    // Не закончено — продолжать слив на следующем service(); голос и AT времени пока не запускать.
    return false;
}

void CellularCore::service() {
    if (!_gsm || !_mqtt || !_web) return;

    if (_bootSettlePending) {
        const uint32_t now = millis();
        if (_bootSettleUntilMs == 0) {
            _bootSettleUntilMs = now + Gsm::POST_BOOT_SETTLE_MS;
            logger.log(LogModule::CellularCore, "GSM pause after start %u ms\n", (unsigned)Gsm::POST_BOOT_SETTLE_MS);
        }
        if ((int32_t)(now - _bootSettleUntilMs) < 0) {
            if (_lastSettleLogMs == 0 || (now - _lastSettleLogMs) >= Cellular::SETTLE_LOG_INTERVAL_MS) {
                _lastSettleLogMs = now;
                const uint32_t left = _bootSettleUntilMs - now;
                logger.log(LogModule::CellularCore, "GSM pause, ~%u ms left\n", (unsigned)left);
            }
            return;
        }
        _bootSettlePending = false;
        logger.log(LogModule::CellularCore, "GSM pause over, starting modem\n");
    }

    if (!_gsmStarted) {
        _gsm->begin();
        _gsmStarted = true;
    }

    _gsm->update();

    // Исключительная эпоха голоса/SMS: слить MQTT до ATD/ATA/CMGS.
    if (_gsm->serviceEpochNeedsMqttDrain()) {
        if (_mqttStarted || (_mqtt && _mqtt->needsDisconnectDrain())) {
            if (_mqttStarted) {
                logger.log(LogModule::CellularCore, "MQTT flush for voice/SMS epoch\n");
            }
            if (!drainMqttDisconnect_()) return;
        } else if (_mqtt) {
            _mqtt->setReconnectEnabled(false);
        }
        _gsm->notifyMqttDrainedForServiceEpoch();
    }

    // Тяжёлый yield синхронизации времени: слить MQTT, чтобы прошёл CCLK→CIPSHUT→CIPGSMLOC→CNTP.
    // CCLK-lite слив не просит. Пропускать, пока голос владеет модемом (взаимное исключение).
    if (_timeSync && _timeSync->needsMqttDrainForSync() && !_gsm->modemServiceEpochBusy() &&
        !_gsm->serviceEpochNeedsMqttDrain()) {
        if (_mqttStarted || (_mqtt && _mqtt->needsDisconnectDrain())) {
            if (_mqttStarted) {
                logger.log(LogModule::CellularCore, "MQTT flush for time-sync yield\n");
            }
            if (!drainMqttDisconnect_()) return;
        } else if (_mqtt) {
            _mqtt->setReconnectEnabled(false);
        }
        _timeSync->notifyMqttDrainedForSync();
    }

    if (_gsm->isReady()) {
        if (_gsm->modemServiceEpochBusy()) {
            // Держать MQTT выключенным всю эпоху голоса/SMS.
            return;
        }

        if (_timeSync && _timeSync->isSyncYieldEpochBusy()) {
            // Держать MQTT выключенным на время слива и каскада (полная синхронизация времени, как на старте).
            _mqtt->setReconnectEnabled(false);
            return;
        }

        // Политика: переподключение MQTT всегда включено (даже при активном UI/SSE).
        // Защита от WDT обеспечивается на транспортном слое (прокачка и бюджеты) для SIM800.
        _mqtt->setReconnectEnabled(true);

        // Упорядочить старт: каскад времени до первого MQTT CIPSTART.
        if (_timeSync && !_timeSync->isBootTimeSettled()) {
            if (!_loggedBootNtpWait) {
                _loggedBootNtpWait = true;
                logger.log(LogModule::CellularCore, "waiting boot time before MQTT\n");
            }
            return;
        }
        if (_loggedBootNtpWait) {
            _loggedBootNtpWait = false;
            logger.log(LogModule::CellularCore, "boot time set, starting MQTT\n");
        }

        if (!_mqttStarted) {
            _mqtt->begin();
            _mqttStarted = true;
        }
        if (_mqttStarted) {
            if (_gsm->takeTcpRxOverflow()) {
                _mqtt->onTransportRxOverflow();
            }
            // Всегда тикать MQTT, чтобы кольцо RX сливалось во время CIPSEND; запись — пустая операция, пока шина занята.
            _mqtt->loop();
        }

        // После живой сессии MQTT сбросить backoff повторного присоединения GSM (не при каждом входе в READY).
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
                logger.log(LogModule::CellularCore, "MQTT connect failures=%u reason=%s, GSM reconnect requested%s\n",
                           (unsigned)failStreak, (why && why[0]) ? why : "?",
                           canRequest ? "" : " (cooldown)");
                core.logHeapSnapshot("mqtt_connect_fail");
            }
        } else {
            _lastLoggedFailStreak = failStreak;
        }
    } else if (_mqttStarted || (_mqtt && _mqtt->needsDisconnectDrain())) {
        // Выход из READY (повторное присоединение/ERROR): слить offline+DISCONNECT, как suspend (C1).
        logger.log(LogModule::CellularCore, "GSM left READY — flushing MQTT\n");
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
    // Держать _bootSettlePending в false — полную паузу settle ждёт только холодный первый старт.
    _lastReattachRequestMs = 0;
    _lastLoggedFailStreak = 0;
    _loggedBootNtpWait = false;
    _mqttStableNoted = false;
}
