// include/common/LogModule.h
#pragma once

#include <stdint.h>

/**
 * Источник строки журнала — обязательный первый аргумент `logger.log()`.
 * Имя печатается в каждой строке: `[123ms] [FsManager] ...`. Реестр и правила — docs/CONVENTIONS.md.
 *
 * Имя модуля совпадает с именем типа, который пишет строку. Два канала названы не по типу:
 * - `AT`      — сырой обмен с модемом (ModemUart, AtSession). UI по подстроке `] [AT] ` отправляет
 *               такие строки в AT-консоль (data/js/app/sse.js), поэтому имя менять нельзя;
 * - `Starter` — шаг ENGINE_START (ProgramExecutor.Starter.cpp).
 *
 * Новый модуль: значение перед `Count` и строка в logModuleName().
 */
enum class LogModule : uint8_t {
    AT,
    BatterySaverManager,
    CellularCore,
    Config,
    Core,
    DigitalInputs,
    ErrorManager,
    FlashCommitCoordinator,
    FsManager,
    GsmController,
    Logger,
    ModeManager,
    MqttClient,
    MqttFsmClient,
    OtaHandler,
    ProgramExecutor,
    RelayController,
    ScheduleTriggerManager,
    SensorsController,
    Sim800TcpTransport,
    Starter,
    ThermostatManager,
    TimeSyncManager,
    TriggerManager,
    WebServer,
    Count
};

inline const char* logModuleName(LogModule m) {
    switch (m) {
        case LogModule::AT:                     return "AT";
        case LogModule::BatterySaverManager:    return "BatterySaverManager";
        case LogModule::CellularCore:           return "CellularCore";
        case LogModule::Config:                 return "Config";
        case LogModule::Core:                   return "Core";
        case LogModule::DigitalInputs:          return "DigitalInputs";
        case LogModule::ErrorManager:           return "ErrorManager";
        case LogModule::FlashCommitCoordinator: return "FlashCommitCoordinator";
        case LogModule::FsManager:              return "FsManager";
        case LogModule::GsmController:          return "GsmController";
        case LogModule::Logger:                 return "Logger";
        case LogModule::ModeManager:            return "ModeManager";
        case LogModule::MqttClient:             return "MqttClient";
        case LogModule::MqttFsmClient:          return "MqttFsmClient";
        case LogModule::OtaHandler:             return "OtaHandler";
        case LogModule::ProgramExecutor:        return "ProgramExecutor";
        case LogModule::RelayController:        return "RelayController";
        case LogModule::ScheduleTriggerManager: return "ScheduleTriggerManager";
        case LogModule::SensorsController:      return "SensorsController";
        case LogModule::Sim800TcpTransport:     return "Sim800TcpTransport";
        case LogModule::Starter:                return "Starter";
        case LogModule::ThermostatManager:      return "ThermostatManager";
        case LogModule::TimeSyncManager:        return "TimeSyncManager";
        case LogModule::TriggerManager:         return "TriggerManager";
        case LogModule::WebServer:              return "WebServer";
        case LogModule::Count:                  break;
    }
    return "?";
}
