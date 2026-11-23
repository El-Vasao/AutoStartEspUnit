/**
 * @file config/Config.h
 * @brief Публичный API менеджера конфигурации.
 */
#pragma once

#include <Arduino.h>

#include "app/AppPorts.h"
#include "config/ConfigTypes.h"
#include "common/ErrorCodes.h"
#include "program/CompiledStep.h"

class SensorsController;

enum class ConfigLoadOutcome : uint8_t {
    OkFromFile,
    OkAfterFactoryDefaultsWrittenRebootRecommended,
    Failed,
};

class Config {
public:
    Config();

    const BaseConfig& getBase() const { return baseCache; }
#ifdef UNIT_TEST
    BaseConfig& testBase() { return baseCache; }
#endif
    /// JSON RAM-кэша тем же emit, что и `/config.json`. ФС не читает.
    void emitBaseJson(Print& out) const;
    uint16_t getCRC() const { return configCRC; }
    bool isLoaded() const { return loaded; }

    /// Последняя отдельная ошибка загрузки (CONFIG_*) или NONE после успешной загрузки.
    ErrorCode lastLoadError() const { return lastLoadError_; }

    void setWdtPort(const WdtPort& port) { wdtPort_ = port; }
    const WdtPort& wdtPort() const { return wdtPort_; }

    ConfigLoadOutcome loadWithOutcome();
    bool load() { return loadWithOutcome() != ConfigLoadOutcome::Failed; }
    /// Заводской `/config.json`. Файлы программ не меняет.
    bool reset();
    bool save();

    /// Обновить часовой пояс настенных часов в RAM; писать во flash только если значение изменилось.
    /// false, если запись во flash была нужна и не удалась.
    bool setTzOffsetHours(int8_t hours);

    /// Применить тело отправленного `/config/save`. Если в теле есть эфемерное
    /// `vehicle.adc_calibrate_voltage`, просит у `sensors` коэффициент (raw остаётся внутри sensors).
    /// Поле калибровки никогда не сохраняется.
    bool applyPostedConfigJsonFile(const char* tmpPath, SensorsController& sensors);
    bool commitPostedProgramFile(const char* tmpPath, uint8_t* outId = nullptr, ErrorCode* outErr = nullptr);

    bool loadProgramCompiled(uint8_t id, CompiledStep* outSteps, uint8_t maxSteps,
                             uint8_t* outCount, char* outName, size_t outNameSize);
    bool rebuildProgramIndex();
    bool deleteProgram(uint8_t id);
    bool resetPrograms();

    /** Только массив `[{id,name},...]` для обёртки reply envelope. */
    bool emitProgramIndexArray(Print& p) const;

    bool isProgramUsed(uint8_t id) const;
    bool ensureProgramIndex();

    /// FNV-1a сырых байт `/programs/<id>.json` в порядке возрастания id. 0, пока индекс не принят.
    uint32_t programsRev() const { return programsRev_; }

private:
    BaseConfig baseCache;
    uint16_t configCRC{};
    bool loaded{};
    ErrorCode lastLoadError_{ErrorCode::NONE};
    WdtPort wdtPort_{};
    uint32_t programsRev_{0};

    void refreshProgramsRevFromRows_(const void* rows, size_t n);

    bool loadBaseFromFile(const char* path, BaseConfig& target, size_t& outLen, uint16_t* outFileCrc = nullptr);
    bool saveBaseConfig(const BaseConfig& cfg);
    bool validateCross(const BaseConfig& cfg);
    /// Записывает заводской набор (1 «Запуск», 2 «Стоп», служебные 230–249) и пересобирает индекс.
    bool writeFactoryPrograms();
};

extern Config config;
