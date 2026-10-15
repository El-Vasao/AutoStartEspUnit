#include "config/Config.h"
#include "io/SensorsController.h"
#include "fs/FsManager.h"
#include "common/Constants.h"
#include "common/ErrorCodes.h"
#include "common/Logger.h"
#include "common/Utils.h"
#include "config/internal/BaseConfigJsonIo.h"
#include "config/internal/ProgramJsonIo.h"
#include "config/internal/ConfigStorageInternal.h"
#include "program/ProgramAction.h"
#include "program/ProgramValidate.h"
#include "common/Pins.h"

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
        f, tmp, config_internal::ParseMode::Strict, &hasAdcCalibrate, &adcCalibrateVoltage);
    f.close();
    fileSystem.deleteFile(tmpPath);
    if (!parsed) {
        logger.log(LogModule::Config, "Posted config rejected: not a complete JSON object\n");
        return false;
    }

    if (hasAdcCalibrate) {
        float coeff = 0.0f;
        if (!sensors.computeAdcVoltageCoeff(adcCalibrateVoltage, coeff)) {
            logger.log(LogModule::Config, "ADC calibrate rejected: V=%.3f (sensors not ready)\n",
                       adcCalibrateVoltage);
            return false;
        }
        tmp.vehicle.adc_voltage_coeff = coeff;
        logger.log(LogModule::Config, "ADC calibrate: V=%.3f coeff=%.6f\n",
                   adcCalibrateVoltage, tmp.vehicle.adc_voltage_coeff);
    } else {
        if (!(tmp.vehicle.adc_voltage_coeff > 0.0f) || !isfinite(tmp.vehicle.adc_voltage_coeff)) {
            logger.log(LogModule::Config, "Posted adc_voltage_coeff invalid; using DEFAULT_COEFF\n");
            tmp.vehicle.adc_voltage_coeff = Adc::DEFAULT_COEFF;
        }
    }

    if (!validateCross(tmp)) {
        logger.log(LogModule::Config, "Posted config failed cross-validation\n");
        return false;
    }

    prepareFlashWriteLogGcAndWdt();

    if (!fileSystem.writeJsonAtomicStream("/config.json", encodePostedBaseConfig, &tmp, Limits::CONFIG_JSON_SIZE)) {
        logger.log(LogModule::Config, "Failed to write config.json\n");
        return false;
    }

    File cf = fileSystem.openRead("/config.json");
    if (cf) {
        configCRC = crc16ModbusStreamFile(cf);
        cf.close();
    }
    logger.log(LogModule::Config, "Posted config written to /config.json, CRC=%04X\n", configCRC);
    return true;
}

bool Config::commitPostedProgramFile(const char* tmpPath, uint8_t* outId, ErrorCode* outErr) {
    if (outErr) *outErr = ErrorCode::NONE;
    File f = fileSystem.openRead(tmpPath);
    if (!f) {
        fileSystem.deleteFile(tmpPath);
        if (outErr) *outErr = ErrorCode::CONFIG_PARSE_FAIL;
        return false;
    }
    Program prog{};
    const bool ok = program_json::parseProgramObjectFile(f, prog);
    f.close();
    fileSystem.deleteFile(tmpPath);
    if (!ok || prog.id == 0) {
        if (outErr) *outErr = ErrorCode::CONFIG_PARSE_FAIL;
        return false;
    }
    const uint16_t starterRelayId = baseCache.vehicle.starter_relay_id;
    uint16_t inTrigIds[Limits::MAX_TRIGGERS];
    uint16_t tempTrigIds[Limits::MAX_TRIGGERS];
    uint8_t nInTrig = 0;
    uint8_t nTempTrig = 0;
    for (uint8_t i = 0; i < baseCache.input_triggers_count && nInTrig < Limits::MAX_TRIGGERS; i++) {
        inTrigIds[nInTrig++] = baseCache.input_triggers[i].id;
    }
    for (uint8_t i = 0; i < baseCache.temperature_triggers_count && nTempTrig < Limits::MAX_TRIGGERS; i++) {
        tempTrigIds[nTempTrig++] = baseCache.temperature_triggers[i].id;
    }
    const program_validate::ChannelTables tables{
        {Pin::RELAY_IDS, HardwareLimits::RELAYS},
        {Pin::INPUT_IDS, HardwareLimits::INPUTS},
        {inTrigIds, nInTrig},
        {tempTrigIds, nTempTrig},
    };
    for (uint8_t i = 0; i < prog.step_count; i++) {
        const Step& s = prog.steps[i];
        const program_validate::ChannelRef ch{s.relay_id, s.input_id, s.input_trigger_id, s.temp_trigger_id};
        const program_validate::Issue issue =
            program_validate::checkStep(actionIdFromString(s.action), ch, starterRelayId, tables);
        if (issue != program_validate::Issue::None) {
            logger.log(LogModule::Config, "Program %u rejected: step %u (%s) %s\n", prog.id, (unsigned)(i + 1),
                       s.action, program_validate::issueToString(issue));
            if (outErr) *outErr = ErrorCode::PROGRAM_INVALID_REF;
            return false;
        }
    }
    if (outId) *outId = prog.id;

    char path[BufferBytes::Fs::PROGRAM_PATH];
    snprintf(path, sizeof(path), "/programs/%u.json", prog.id);

    if (!ensureProgramsDir()) return false;

    prepareFlashWriteLogGcAndWdt();

    if (!fileSystem.writeJsonAtomicStream(path, encodePostedProgram, &prog, Limits::CONFIG_JSON_SIZE)) {
        logger.log(LogModule::Config, "Failed to write program file\n");
        return false;
    }

    logger.log(LogModule::Config, "Program %u file written\n", prog.id);
    return true;
}
