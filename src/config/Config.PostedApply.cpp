#include "config/Config.h"
#include "io/SensorsController.h"
#include "fs/FSManager.h"
#include "common/Constants.h"
#include "common/Logger.h"
#include "common/Utils.h"
#include "config/internal/BaseConfigJsonIo.h"
#include "config/internal/ProgramJsonIo.h"
#include "config/internal/ConfigStorageInternal.h"

#include <math.h>

using namespace config_storage_internal;

namespace {

size_t encodePostedBaseConfig(Print& p, void* ctx) {
    const auto* cfg = reinterpret_cast<const BaseConfig*>(ctx);
    return config_internal::serializeBaseConfigToPrint(*cfg, p);
}

size_t encodePostedProgram(Print& p, void* ctx) {
    const auto* prog = reinterpret_cast<const Program*>(ctx);
    return program_json::emitProgramToPrint(*prog, p);
}

/// Old formula was `raw * (3.3/4095) * 15 * coeff`. New is `raw * coeff`.
/// Legacy configs often store coeff≈10; fold the old scale in once when coeff looks legacy.
void foldLegacyAdcVoltageCoeff(float& coeff) {
    if (!(coeff >= 1.0f)) return;
    coeff *= (3.3f / 4095.0f) * 15.0f;
}

} // namespace

bool Config::applyPostedConfigJsonFile(const char* tmpPath, SensorsController& sensors) {
    File f = fileSystem.openRead(tmpPath);
    if (!f) {
        fileSystem.deleteFile(tmpPath);
        return false;
    }
    BaseConfig tmp{};
    bool hasAdcCalibrate = false;
    float adcCalibrateVoltage = 0.0f;
    const bool parsed = config_internal::parseBaseConfigStreamingFromFile(
        f, tmp, &hasAdcCalibrate, &adcCalibrateVoltage);
    f.close();
    fileSystem.deleteFile(tmpPath);
    if (!parsed) {
        return false;
    }

    if (hasAdcCalibrate) {
        float coeff = 0.0f;
        if (!sensors.computeAdcVoltageCoeff(adcCalibrateVoltage, coeff)) {
            logger.log("[Config] ADC calibrate rejected: V=%.3f (sensors not ready)\n",
                       adcCalibrateVoltage);
            return false;
        }
        tmp.vehicle.adc_voltage_coeff = coeff;
        logger.log("[Config] ADC calibrate: V=%.3f coeff=%.6f\n",
                   adcCalibrateVoltage, tmp.vehicle.adc_voltage_coeff);
    } else {
        foldLegacyAdcVoltageCoeff(tmp.vehicle.adc_voltage_coeff);
        if (!(tmp.vehicle.adc_voltage_coeff > 0.0f) || !isfinite(tmp.vehicle.adc_voltage_coeff)) {
            logger.log("[Config] Posted adc_voltage_coeff invalid; using DEFAULT_COEFF\n");
            tmp.vehicle.adc_voltage_coeff = ADC::DEFAULT_COEFF;
        }
    }

    if (!validateCross(tmp)) {
        logger.log("[Config] Posted config failed cross-validation\n");
        return false;
    }

    prepareFlashWriteLogGcAndWdt();

    if (!fileSystem.writeJsonAtomicStream("/config.json", encodePostedBaseConfig, &tmp, Limits::CONFIG_JSON_SIZE)) {
        logger.log("[Config] Failed to write config.json\n");
        return false;
    }

    File cf = fileSystem.openRead("/config.json");
    if (cf) {
        configCRC = crc16ModbusStreamFile(cf);
        cf.close();
    }
    logger.log("[Config] Posted config written to /config.json, CRC=%04X\n", configCRC);
    return true;
}

bool Config::commitPostedProgramFile(const char* tmpPath, uint8_t* outId) {
    File f = fileSystem.openRead(tmpPath);
    if (!f) {
        fileSystem.deleteFile(tmpPath);
        return false;
    }
    Program prog{};
    const bool ok = program_json::parseProgramObjectFile(f, prog);
    f.close();
    fileSystem.deleteFile(tmpPath);
    if (!ok || prog.id == 0) {
        return false;
    }
    if (outId) *outId = prog.id;

    char path[BufferBytes::Fs::PROGRAM_PATH];
    snprintf(path, sizeof(path), "/programs/%u.json", prog.id);

    if (!ensureProgramsDir()) return false;

    prepareFlashWriteLogGcAndWdt();

    if (!fileSystem.writeJsonAtomicStream(path, encodePostedProgram, &prog, Limits::CONFIG_JSON_SIZE)) {
        logger.log("[Config] Failed to write program file\n");
        return false;
    }

    logger.log("[Config] Program %u file written\n", prog.id);
    return true;
}
