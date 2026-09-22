/**
 * @file config/Config.h
 * @brief Публичный API менеджера конфигурации.
 */
#pragma once

#include <Arduino.h>

#include "app/AppPorts.h"
#include "config/ConfigTypes.h"
#include "program/CompiledStep.h"

enum class ConfigLoadOutcome : uint8_t {
    OkFromFile,
    OkAfterFactoryDefaultsWrittenRebootRecommended,
    Failed,
};

class Config {
public:
    Config();

    const BaseConfig& getBase() const { return baseCache; }
    uint16_t getCRC() const { return configCRC; }
    bool isLoaded() const { return loaded; }

    void setWdtPort(const WdtPort& port) { wdtPort_ = port; }
    const WdtPort& wdtPort() const { return wdtPort_; }

    ConfigLoadOutcome loadWithOutcome();
    bool load() { return loadWithOutcome() != ConfigLoadOutcome::Failed; }
    bool reset();
    bool save();

    /// Сериализовать текущий `baseCache` в JSON (wire-формат как раньше; сейчас — потоковая печать, без ArduinoJson).
    void emitCurrentBaseConfigJson(Print& p) const;

    bool applyPostedConfigJsonFile(const char* tmpPath);
    bool commitPostedProgramFile(const char* tmpPath, uint8_t* outId = nullptr);

    bool loadProgramCompiled(uint8_t id, CompiledStep* outSteps, uint8_t maxSteps,
                             uint8_t* outCount, char* outName, size_t outNameSize);
    bool writeProgramFile(const Program& prog);
    bool rebuildProgramIndex();
    bool saveProgram(const Program& prog);
    bool deleteProgram(uint8_t id);
    bool resetPrograms();

    /** `{"programs":[{id,name},...]}` из `/programs/index.json` (для MQTT и др.). */
    bool emitProgramListWrapped(Print& p) const;

    /** Только массив `[{id,name},...]` для обёртки reply envelope. */
    bool emitProgramIndexArray(Print& p) const;

    bool isProgramUsed(uint8_t id) const;
    bool ensureProgramIndex();

private:
    BaseConfig baseCache;
    uint16_t configCRC{};
    bool loaded{};
    WdtPort wdtPort_{};

    bool loadBaseFromFile(const char* path, BaseConfig& target, size_t& outLen, uint16_t* outFileCrc = nullptr);
    bool saveBaseConfig(const BaseConfig& cfg);
    bool validateCross(const BaseConfig& cfg);

    bool programExists(uint8_t id);
};

extern Config config;
