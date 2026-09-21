// include/core/Core.h
#pragma once

#include <Arduino.h>
#include "common/EspHal.h"

#include "common/Version.h"
#include "common/Constants.h"

// Forward declarations to keep Core.h lightweight.
class RelayController;
class SensorsController;
class DigitalInputs;
class ProgramExecutor;
class GSMController;
class MQTTClient;
class ErrorManager;

struct CorePrivate;

/**
 * @brief Ядро системы, связывает все компоненты и управляет режимами.
 *
 * Идея:
 * - `Core::begin()` инициализирует всё “железо” и конфиг, затем выбирает стартовый режим.
 * - `Core::update()` — единственная “точка правды” для loop(): watchdog, периодика, обработка текущего режима.
 *
 * Поддерживаемость:
 * - управляющие подсистемы (AP/OTA/связь) переключаются через `ModeManager`;
 * - функциональные менеджеры (триггеры/термостат/battery saver) обновляются в NORMAL-режиме.
 *
 * Продукт: в `CoreMode::NORMAL` веб UI считается постоянно доступным (см. `WebServer.h`, `docs/PRODUCT.md`).
 */
class Core {
public:
    Core();

    // Инициализация всех подсистем, запуск в начальном режиме
    bool begin();

    // Главный цикл, вызывается из loop()
    void update();

    // Версия прошивки (числовое представление)
    uint32_t getVersion() const { return FIRMWARE_VERSION_NUM; }

    // Версия прошивки (строка)
    const char* getVersionString() const { return FIRMWARE_VERSION_STR; }

    // Аптайм в секундах
    uint32_t getUptime() const;

    // Текущий режим работы
    CoreMode getMode() const;

    // Имя текущего режима (строка)
    const char* getModeName() const;

    // После применения настроек требуется перезагрузка
    bool isRebootRequired() const;
    void setRebootRequired(bool v);

    // Свободная память кучи
    uint32_t getFreeHeap() const { return espHalFreeHeap(); }

    /// Снимок кучи (free / max block / fragmentation + min с момента загрузки). `tag` — опциональная метка события.
    void logHeapSnapshot(const char* tag = nullptr) const;

    // Причина последней перезагрузки
    const char* getResetReason() const;

    // Перезагрузка устройства
    void reboot();

    // Запросить перезагрузку (выполняется из Core::update/loop)
    void requestReboot(uint32_t delayMs = 0);

    // Сброс к заводским настройкам (удаление конфига)
    void factoryReset();

    // Переключение в режим OTA-обновления (`/update.bin` уже готов, см. `POST /ota/start`).
    void startOTAUpdate();

    /// `POST /upload` после `openWriteStream`: лёгкий путь из AsyncTCP (режим/бродкаст — в `Core::update`).
    void onOtaHttpUploadStreamOpenedFromWeb();

    /// Итог HTTP upload: при `ok==false` ядро вернётся в NORMAL без ребута.
    void notifyOtaHttpUploadComplete(bool ok);

    void exitOtaToNormalMode();

    /// Таймаут ожидания `final` у multipart upload — помечает веб и выходит в NORMAL.
    void onOtaHttpUploadAwaitTimedOut();

    /// Пред-OTA окно: upload уже идёт, но `CoreMode::OTA_UPDATE` ещё не активирован.
    bool isOtaUploadPressureActive() const;
    /// Internal lifecycle hook: enable/disable pre-OTA pressure mode.
    void setOtaUploadPressureActive(bool active, const char* reasonTag);

    // Подкармливание watchdog (должно вызываться в длительных операциях)
    void feedWatchdog();

    /// “Отдать управление” SDK (WiFi/lwIP/таймеры), троттлинг по времени.
    /// Использовать в потенциально длинных циклах, где нет delay()/yield().
    void cooperate();

    // Доступ к подсистемам
    RelayController& getRelay();
    SensorsController& getSensors();
    DigitalInputs& getInputs();
    ProgramExecutor& getProgramExecutor();
    GSMController& getGSM();
    MQTTClient& getMQTT();
    ErrorManager& getErrorManager();

    // Runtime управление (делегируется менеджерам)
    bool getThermostatRuntime() const;
    void setThermostatRuntime(bool en);

    bool getBatterySaverRuntime() const;
    void setBatterySaverRuntime(bool en);

    bool getTriggerRuntime(uint8_t index) const;
    void setTriggerRuntime(uint8_t index, bool en);

    bool getTempTriggerRuntime(uint8_t index) const;
    void setTempTriggerRuntime(uint8_t index, bool en);

    /// Сводная статистика через `logger` (SSE/UI).
    void printStats();

    // Статус “двигатель работает” (для UI/логики)
    bool isEngineRunning() const;

private:
    CorePrivate* _impl{nullptr};

    // Обновление счётчика аптайма
    void updateUptime();

    // Обновление статуса двигателя (дёшево, можно вызывать часто)
    void updateEngineRunning();

    // Управление GSM/MQTT lifecycle в рабочих режимах
    void serviceCellularLink();
    void suspendCellularLink();

    // Периодические задачи (ФС, вывод ошибок, статистика)
    void performPeriodicTasks(uint32_t now);

    // Массив указателей на обработчики режимов
    void (Core::*_modeHandlers[6])();

    // Обработчики для каждого режима
    void handleBoot();
    void handleEmergencyAP();
    void handleSetupAP();
    void handleNormal();
    void handleNormalSilent();
    void handleOTAUpdate();

    // Запрет копирования
    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;
};

extern Core core;

