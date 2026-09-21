#pragma once

#include "config/Config.h"
#include "app/AppPorts.h"

#include "core/BatterySaverManager.h"
#include "io/DigitalInputs.h"
#include "core/ErrorManager.h"
#include "gsm/GSMController.h"
#include "mqtt/MQTTClient.h"
#include "core/ModeManager.h"
#include "ota/OTAHandler.h"
#include "program/ProgramExecutor.h"
#include "io/RelayController.h"
#include "io/SensorsController.h"
#include "core/ThermostatManager.h"
#include "core/TriggerManager.h"
#include "core/internal/CellularCore.h"

/**
 * @file core/internal/CorePrivate.h
 * @brief Внутреннее хранилище `Core` (Pimpl-like, без heap-аллокаций).
 *
 * Контракт:
 * - заголовок относится к реализации `core` и не должен включаться вне `src/core/...`;
 * - все подсистемы хранятся как поля (статическая композиция), чтобы RAM-профиль был предсказуем;
 * - порядок полей важен: зависимости (например, `MQTTClient`) инициализируются после своих провайдеров (`GSMController`).
 *
 * Зачем отдельный struct:
 * - публичный `include/core/Core.h` остаётся лёгким (без тяжелых include’ов);
 * - мы избегаем динамических аллокаций: `Core` держит указатель на глобальный `g_coreImpl`.
 */
struct CorePrivate {
    enum class BootStage : uint8_t {
        InitWiFiOff = 0,
        MountFS,
        LoadOrCreateConfig,
        EnsureProgramIndex,
        InitHardware,
        InitManagers,
        SelectInitialMode,
        Done,
    };

    bool rebootRequired{false};
    bool rebootRequested{false};
    uint32_t rebootAtMs{0};
    /// Рестарт после factory/FS fatal откладываем на следующий `loop`: `ESP.restart()` из глубокого стека после Config FS часто «зависает».
    bool pendingHardRestart{false};
    /// POST /upload открыл поток: тяжёлый `switchMode(OTA)` откладываем в `Core::update` после `webServer.update()`.
    bool pendingDeferredOtaFromWebUpload{false};
    /// true в окне upload->switchMode(OTA): используем для разгрузки GSM/MQTT и SSE/log давления.
    bool otaUploadPressureActive{false};
    BootStage bootStage{BootStage::InitWiFiOff};
    CoreMode bootTargetMode{CoreMode::BOOT};
    bool bootConfigLoaded{false};
    bool bootHwMapBad{false};

    // Subsystems (construct first).
    SensorsController sensors;
    RelayController relay;
    DigitalInputs inputs;
    ProgramExecutor programExecutor;
    GSMController gsm;
    MQTTClient mqtt{gsm.getClient()};
    CellularCore cellular;

    // Managers
    ModeManager modeManager;
    OTAHandler otaHandler;
    ErrorManager errorManager;

    TriggerManager triggerManager{config, inputs, sensors, programExecutor};
    BatterySaverManager batterySaverManager{config, sensors, programExecutor};
    ThermostatManager thermostatManager{config, sensors, programExecutor};

    // State/counters
    uint32_t lastUptimeSecond{0};
    uint32_t lastWdtFeed{0};
    uint32_t lastCooperateMs{0};
    uint32_t lastFsMaintenance{0};
    uint32_t lastErrorReport{0};
    uint32_t lastStatsPrint{0};
    uint32_t loopCounter{0};
    uint32_t maxLoopTime{0};

    bool engineRunning{false};

    // Ports for transports/control glue.
    AppPorts ports{};
};

extern CorePrivate g_coreImpl;

