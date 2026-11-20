#include "config/Config.h"
#include "fs/FsManager.h"
#include "common/Logger.h"
#include "common/Utils.h"
#include "config/internal/ProgramJsonIo.h"
#include "config/internal/ConfigStorageInternal.h"

#include <WiFi.h>

#include <stdlib.h>

using namespace config_storage_internal;

bool Config::loadProgramCompiled(uint8_t id, CompiledStep* outSteps, uint8_t maxSteps, uint8_t* outCount,
                                 char* outName, size_t outNameSize) {
    if (!outSteps || maxSteps == 0 || !outCount || !outName || outNameSize == 0) return false;

    char path[BufferBytes::Fs::PROGRAM_PATH];
    program_json::formatProgramPath(path, sizeof(path), id);

    File f = fileSystem.openRead(path);
    if (!f) {
        logger.log(LogModule::Config, "Failed to read program %u\n", id);
        return false;
    }

    outName[0] = '\0';
    *outCount = 0;
    const bool ok = program_json::parseProgramCompiledFile(f, id, outSteps, maxSteps, outCount, outName, outNameSize);
    f.close();
    return ok;
}

bool Config::isProgramUsed(uint8_t id) const {
    if (!loaded) return false;
    const uint8_t nIn = (baseCache.input_triggers_count < Limits::MAX_TRIGGERS)
                            ? baseCache.input_triggers_count
                            : Limits::MAX_TRIGGERS;
    for (uint8_t i = 0; i < nIn; i++) {
        if (baseCache.input_triggers[i].enabled && baseCache.input_triggers[i].program_id == id) return true;
    }
    const uint8_t nTemp = (baseCache.temperature_triggers_count < Limits::MAX_TRIGGERS)
                              ? baseCache.temperature_triggers_count
                              : Limits::MAX_TRIGGERS;
    for (uint8_t i = 0; i < nTemp; i++) {
        if (baseCache.temperature_triggers[i].enabled && baseCache.temperature_triggers[i].program_id == id) return true;
    }
    const uint8_t nSched = (baseCache.schedule_triggers_count < Limits::MAX_SCHEDULE_TRIGGERS)
                               ? baseCache.schedule_triggers_count
                               : Limits::MAX_SCHEDULE_TRIGGERS;
    for (uint8_t i = 0; i < nSched; i++) {
        if (baseCache.schedule_triggers[i].enabled && baseCache.schedule_triggers[i].program_id == id) return true;
    }
    if (baseCache.thermostat.enabled) {
        if (baseCache.thermostat.program_id_lower == id || baseCache.thermostat.program_id_upper == id) return true;
    }
    if (baseCache.battery_saver.enabled && baseCache.battery_saver.program_id == id) return true;
    return false;
}

bool Config::deleteProgram(uint8_t id) {
    if (isProgramUsed(id)) {
        logger.log(LogModule::Config, "Program %u is in use, cannot delete\n", id);
        return false;
    }

    char path[BufferBytes::Fs::PROGRAM_PATH];
    program_json::formatProgramPath(path, sizeof(path), id);
    if (!fileSystem.deleteFile(path)) return false;

    if (!rebuildProgramIndex()) {
        logger.log(LogModule::Config, "Program %u file removed but index rebuild failed\n", id);
        return false;
    }
    logger.log(LogModule::Config, "Program %u deleted\n", id);
    return true;
}

bool Config::resetPrograms() {
    logger.log(LogModule::Config, "resetPrograms: start\n");
    if (!ensureProgramsDir()) return false;

    bool ok = true;
    File root = fileSystem.openDir("/programs");
    if (root) {
        File entry = root.openNextFile();
        while (entry) {
            wdtPort_.feedNow();
            wdtPort_.feedNow();
            const char* fns = entry.name();
            if (!fns) {
                entry = root.openNextFile();
                continue;
            }
            size_t n = strlen(fns);
            if (n < 6) {
                entry = root.openNextFile();
                continue;
            }
            if (strcmp(fns + (n - 5), ".json") != 0) {
                entry = root.openNextFile();
                continue;
            }
            if (n >= 10 && strcmp(fns + (n - 10), "/index.json") == 0) {
                entry = root.openNextFile();
                continue;
            }
            if (strcmp(fns, "index.json") == 0) {
                entry = root.openNextFile();
                continue;
            }

            char pathBuf[BufferBytes::Fs::PROGRAM_PATH];
            // ESP32 может вернуть имя файла или полный путь; приводим к /programs/<name>.
            if (fns[0] == '/') {
                strlcpy(pathBuf, fns, sizeof(pathBuf));
            } else {
                snprintf(pathBuf, sizeof(pathBuf), "/programs/%s", fns);
            }
            entry.close();
            if (!fileSystem.deleteFile(pathBuf)) {
                ok = false;
            }
            entry = root.openNextFile();
        }
        root.close();
    }

    if (!rebuildProgramIndex()) ok = false;

    logger.log(LogModule::Config, "resetPrograms: %s\n", ok ? "OK" : "FAILED");
    return ok;
}
