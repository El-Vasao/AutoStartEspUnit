// include/core/ModeManager.h
#pragma once

#include <Arduino.h>
#include "common/Constants.h"

// Предварительные объявления
class WebServer;
class GsmController;
class MqttClient;
class Config;
class OtaHandler;

/**
 * @brief Управление режимами работы системы.
 * Обеспечивает переключение между режимами и вызов соответствующих методов входа/выхода.
 *
 * Контракт:
 * - режимы должны включать/выключать только “внешние” подсистемы (AP, OTA, связь),
 *   а основная логика (IO, программы, триггеры) живёт в `Core::handle*`.
 *
 */
class ModeManager {
public:
    ModeManager();

    // Инициализация ссылками на подсистемы (вызывается из Core)
    void init(WebServer& webServer, GsmController& gsm, MqttClient& mqtt,
              Config& config, OtaHandler& ota);

    // Переключиться в новый режим
    void switchMode(CoreMode newMode);

    // Текущий режим
    CoreMode getCurrentMode() const { return _currentMode; }

    // Имя текущего режима
    const char* getModeName() const;

private:
    CoreMode _currentMode;          ///< текущий режим

    WebServer* _webServer;          ///< ссылка на веб-сервер
    GsmController* _gsm;            ///< ссылка на GSM-контроллер
    MqttClient* _mqtt;              ///< ссылка на MQTT-клиент
    Config* _config;                ///< ссылка на конфигурацию
    OtaHandler* _ota;               ///< ссылка на обработчик OTA

    // Методы входа/выхода для каждого режима
    void enterBoot();
    void exitBoot();
    void enterEmergencyAP();
    void exitEmergencyAP();
    void enterSetupAP();
    void exitSetupAP();
    void enterNormal();
    void exitNormal();
    void enterNormalSilent();
    void exitNormalSilent();
    void enterOTAUpdate();
    void exitOTAUpdate();
};

