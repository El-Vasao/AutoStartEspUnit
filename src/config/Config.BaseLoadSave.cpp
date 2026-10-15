#include "config/Config.h"
#include "common/Constants.h"
#include "common/ErrorCodes.h"
#include "common/EspHal.h"
#include "fs/FsManager.h"
#include "common/Logger.h"
#include "common/Utils.h"
#include "config/internal/BaseConfigJsonIo.h"
#include "config/internal/ConfigStorageInternal.h"
#include "config/VehicleRules.h"

#include <math.h>
#include <WiFi.h>
#include <stdlib.h>

Config config;

Config::Config() : configCRC(0), loaded(false), lastLoadError_(ErrorCode::NONE) {}

namespace {

size_t encodeBaseConfigForSave(Print& p, void* ctx) {
    const auto* cfg = reinterpret_cast<const BaseConfig*>(ctx);
    return config_internal::serializeBaseConfigToPrint(*cfg, p);
}

void logVehicleFix(const char* msg, void*) {
    logger.log(LogModule::Config, "fix: %s\n", msg);
}

} // namespace

bool Config::loadBaseFromFile(const char* path, BaseConfig& target, size_t& outLen, uint16_t* outFileCrc) {
    File f = fileSystem.openRead(path);
    if (!f) {
        logger.log(LogModule::Config, "Failed to open file: %s\n", path);
        lastLoadError_ = ErrorCode::CONFIG_MISSING;
        return false;
    }

    const size_t len = f.size();
    if (len == 0 || len >= Limits::CONFIG_JSON_SIZE) {
        logger.log(LogModule::Config, "Invalid config file size: %u\n", (unsigned)len);
        f.close();
        lastLoadError_ = ErrorCode::CONFIG_CRC_FAIL;
        return false;
    }
#ifdef SERIAL_DEBUG
    logger.log(LogModule::Config, "File read OK, length %u\n", (unsigned)len);
#endif
    outLen = len;

    if (outFileCrc) {
        f.seek(0, SeekSet);
        *outFileCrc = crc16ModbusStreamFile(f);
    }

    f.seek(0, SeekSet);
    if (!config_internal::parseBaseConfigStreamingFromFile(f, target, config_internal::ParseMode::Lenient)) {
        f.close();
        logger.log(LogModule::Config, "JSON parse error (streaming)\n");
        lastLoadError_ = ErrorCode::CONFIG_PARSE_FAIL;
        return false;
    }
    f.close();

    // Never fail the load on rule violations: a failed load wipes the user config via reset().
    vehicle_rules::normalize(target, logVehicleFix, nullptr);

    lastLoadError_ = ErrorCode::NONE;
    return true;
}

bool Config::saveBaseConfig(const BaseConfig& cfg) {
    size_t len = 0;
    configCRC = config_internal::crc16SerializedBaseConfig(cfg, &len);
    espHalFeedWdt();
    if (len == 0 || len >= Limits::CONFIG_JSON_SIZE) {
        logger.log(LogModule::Config, "Serialization failed/too large (len=%u)\n", (unsigned)len);
        return false;
    }

    wdtPort_.feedNow();
    espHalFeedWdt();
    if (!fileSystem.writeJsonAtomicStream("/config.json", encodeBaseConfigForSave, const_cast<BaseConfig*>(&cfg),
                                          Limits::CONFIG_JSON_SIZE)) {
        logger.log(LogModule::Config, "Failed to save base config\n");
        return false;
    }

    logger.log(LogModule::Config, "Base config saved successfully, CRC=%04X\n", configCRC);
    return true;
}

ConfigLoadOutcome Config::loadWithOutcome() {
    logger.log(LogModule::Config, "begin\n");
    lastLoadError_ = ErrorCode::NONE;

    // Важно: `BaseConfig` большой; при ошибке файла вызываем `reset()`, где на стеке ещё один `BaseConfig`.
    // Держим `tmp` в отдельном блоке, чтобы к моменту `reset()` он уже был уничтожен — иначе stack smashing на ESP8266.
    {
        BaseConfig tmp;
        size_t fileLen = 0;
        uint16_t fileCrc = 0;
        if (loadBaseFromFile("/config.json", tmp, fileLen, &fileCrc)) {
            if (!(tmp.vehicle.adc_voltage_coeff > 0.0f) || !isfinite(tmp.vehicle.adc_voltage_coeff)) {
                logger.log(LogModule::Config, "adc_voltage_coeff invalid (%.6f); using DEFAULT_COEFF\n",
                           tmp.vehicle.adc_voltage_coeff);
                tmp.vehicle.adc_voltage_coeff = Adc::DEFAULT_COEFF;
            }
            baseCache = tmp;
            loaded = true;
            configCRC = fileCrc;
            logger.log(LogModule::Config, "Config loaded successfully, CRC=%04X\n", configCRC);
            return ConfigLoadOutcome::OkFromFile;
        }
    }

    logger.log(LogModule::Config, "Config load failed, resetting to defaults\n");
    if (!reset()) {
        logger.log(LogModule::Config, "Reset failed\n");
        if (lastLoadError_ == ErrorCode::NONE) {
            lastLoadError_ = ErrorCode::CONFIG_MISSING;
        }
        return ConfigLoadOutcome::Failed;
    }
    logger.log(LogModule::Config, "Defaults written and loaded into RAM, CRC=%04X; rebooting for clean startup\n", configCRC);
    return ConfigLoadOutcome::OkAfterFactoryDefaultsWrittenRebootRecommended;
}

bool Config::save() { return saveBaseConfig(baseCache); }

bool Config::setTzOffsetHours(int8_t hours) {
    if (hours < -12 || hours > 14) {
        logger.log(LogModule::Config, "reject tz_offset_hours=%d\n", (int)hours);
        return false;
    }
    if (baseCache.time.tz_offset_hours == hours) return true;
    const int8_t prev = baseCache.time.tz_offset_hours;
    baseCache.time.tz_offset_hours = hours;
    if (!save()) {
        baseCache.time.tz_offset_hours = prev;
        logger.log(LogModule::Config, "setTzOffsetHours save failed, reverted to %+dh\n", (int)prev);
        return false;
    }
    logger.log(LogModule::Config, "tz_offset_hours %+dh → %+dh\n", (int)prev, (int)hours);
    return true;
}

void Config::emitCurrentBaseConfigJson(Print& p) const {
    config_internal::serializeBaseConfigToPrint(baseCache, p);
}

namespace config_storage_internal {
void prepareFlashWriteLogGcAndWdt() {
    size_t freeSpace = fileSystem.getFreeSpace();
    logger.log(LogModule::Config, "Free space: %u bytes\n", freeSpace);
    if (freeSpace < FsSpace::MIN_FREE_SPACE) {
        logger.log(LogModule::Config, "Low free space, reclaiming orphans…\n");
        fileSystem.gc();
        freeSpace = fileSystem.getFreeSpace();
        logger.log(LogModule::Config, "Free space after reclaim: %u bytes\n", freeSpace);
    }
    config.wdtPort().feedNow();
    espHalFeedWdt();
}

bool ensureProgramsDir() {
    if (!fileSystem.exists("/programs")) {
        logger.log(LogModule::Config, "Directory /programs does not exist, creating...\n");
        if (!fileSystem.mkdir("/programs")) {
            logger.log(LogModule::Config, "ERROR: Failed to create /programs directory\n");
            return false;
        }
    }
    return true;
}
} // namespace config_storage_internal
