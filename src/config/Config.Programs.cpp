#include "config/Config.h"
#include "fs/FsManager.h"
#include "common/Logger.h"
#include "common/Utils.h"
#include "config/internal/ProgramJsonIo.h"
#include "config/internal/ConfigStorageInternal.h"

#include <WiFi.h>

#include <stdlib.h>
#include <string.h>

using namespace config_storage_internal;

namespace {

size_t encodeFactoryProgram(Print& p, void* ctx) {
    const auto* prog = reinterpret_cast<const Program*>(ctx);
    return program_json::emitProgramToPrint(*prog, p);
}

void fillStep(Step& s, uint8_t n, const char* action, uint8_t relayNumber = 0, uint32_t ms = 0,
              const char* message = nullptr) {
    s = Step{};
    s.step = n;
    strlcpy(s.action, action, sizeof(s.action));
    s.relay_id = relayNumber;
    s.ms = ms;
    if (message) strlcpy(s.message, message, sizeof(s.message));
}

void fillTriggerStep(Step& s, uint8_t n, const char* action, uint16_t inputTriggerId, uint16_t tempTriggerId) {
    s = Step{};
    s.step = n;
    strlcpy(s.action, action, sizeof(s.action));
    s.input_trigger_id = inputTriggerId;
    s.temp_trigger_id = tempTriggerId;
}

bool writeOneProgram(const Program& prog) {
    char path[BufferBytes::Fs::PROGRAM_PATH];
    program_json::formatProgramPath(path, sizeof(path), prog.id);
    prepareFlashWriteLogGcAndWdt();
    if (!fileSystem.writeJsonAtomicStream(path, encodeFactoryProgram, const_cast<Program*>(&prog),
                                          Limits::CONFIG_JSON_SIZE)) {
        logger.log(LogModule::Config, "failed to write factory program %u\n", prog.id);
        return false;
    }
    return true;
}

void clearProgram(Program& prog) {
    prog = Program{};
}

bool writeNamedAction(Program& prog, uint8_t id, const char* name, const char* action,
                      uint8_t relayNumber = 0, const char* message = nullptr) {
    clearProgram(prog);
    prog.id = id;
    strlcpy(prog.name, name, sizeof(prog.name));
    fillStep(prog.steps[0], 1, action, relayNumber, 0, message);
    prog.step_count = 1;
    return writeOneProgram(prog);
}

bool writeAllRelays(Program& prog, uint8_t id, const char* name, const char* action) {
    // K1 ACC, K2 IGN, K4 LOCK, K5 IMMO — без K3 (стартер).
    static const uint8_t kRelays[] = {1, 2, 4, 5};
    clearProgram(prog);
    prog.id = id;
    strlcpy(prog.name, name, sizeof(prog.name));
    for (uint8_t i = 0; i < 4; i++) {
        fillStep(prog.steps[i], static_cast<uint8_t>(i + 1), action, kRelays[i]);
    }
    prog.step_count = 4;
    return writeOneProgram(prog);
}

bool writeInputTriggers(Program& prog, uint8_t id, const char* name, const char* action) {
    clearProgram(prog);
    prog.id = id;
    strlcpy(prog.name, name, sizeof(prog.name));
    for (uint8_t i = 0; i < 3; i++) {
        fillTriggerStep(prog.steps[i], static_cast<uint8_t>(i + 1), action,
                        static_cast<uint16_t>(i + 1), 0);
    }
    prog.step_count = 3;
    return writeOneProgram(prog);
}

bool writeTempTriggers(Program& prog, uint8_t id, const char* name, const char* action) {
    clearProgram(prog);
    prog.id = id;
    strlcpy(prog.name, name, sizeof(prog.name));
    for (uint8_t i = 0; i < 3; i++) {
        fillTriggerStep(prog.steps[i], static_cast<uint8_t>(i + 1), action, 0,
                        static_cast<uint16_t>(i + 1));
    }
    prog.step_count = 3;
    return writeOneProgram(prog);
}

} // namespace

bool Config::writeFactoryPrograms() {
    logger.log(LogModule::Config, "writing factory programs\n");
    if (!ensureProgramsDir()) return false;

    Program prog{};

    // K5 bypass, K2 ignition, K1 ACC — номера шёлка 5/2/1. Без SMS.
    clearProgram(prog);
    prog.id = 1;
    strlcpy(prog.name, "Запуск", sizeof(prog.name));
    fillStep(prog.steps[0], 1, "RELAY_ON", 5);
    fillStep(prog.steps[1], 2, "WAIT", 0, 500);
    fillStep(prog.steps[2], 3, "RELAY_ON", 2);
    fillStep(prog.steps[3], 4, "WAIT", 0, 5000);
    fillStep(prog.steps[4], 5, "ENGINE_START");
    fillStep(prog.steps[5], 6, "RELAY_ON", 1);
    fillStep(prog.steps[6], 7, "WAIT", 0, 5000);
    fillStep(prog.steps[7], 8, "RELAY_OFF", 5);
    prog.step_count = 8;
    if (!writeOneProgram(prog)) return false;

    clearProgram(prog);
    prog.id = 2;
    strlcpy(prog.name, "Стоп", sizeof(prog.name));
    fillStep(prog.steps[0], 1, "RELAY_OFF", 2);
    fillStep(prog.steps[1], 2, "RELAY_OFF", 1);
    fillStep(prog.steps[2], 3, "RELAY_OFF", 5);
    prog.step_count = 3;
    if (!writeOneProgram(prog)) return false;

    // Служебные id 230–249 — внизу списка (сортировка по возрастанию id).
    if (!writeAllRelays(prog, 230, "Все вкл", "RELAY_ON")) return false;
    if (!writeAllRelays(prog, 231, "Все выкл", "RELAY_OFF")) return false;
    if (!writeNamedAction(prog, 232, "K1 вкл", "RELAY_ON", 1)) return false;
    if (!writeNamedAction(prog, 233, "K1 выкл", "RELAY_OFF", 1)) return false;
    if (!writeNamedAction(prog, 234, "K2 вкл", "RELAY_ON", 2)) return false;
    if (!writeNamedAction(prog, 235, "K2 выкл", "RELAY_OFF", 2)) return false;
    if (!writeNamedAction(prog, 236, "K4 вкл", "RELAY_ON", 4)) return false;
    if (!writeNamedAction(prog, 237, "K4 выкл", "RELAY_OFF", 4)) return false;
    if (!writeNamedAction(prog, 238, "K5 вкл", "RELAY_ON", 5)) return false;
    if (!writeNamedAction(prog, 239, "K5 выкл", "RELAY_OFF", 5)) return false;
    if (!writeNamedAction(prog, 240, "SMS", "SMS_OWNER", 0, "Test")) return false;
    if (!writeNamedAction(prog, 241, "Звонок", "CALL_OWNER")) return false;
    if (!writeNamedAction(prog, 242, "АКБ вкл", "BATTERY_SAVER_ON")) return false;
    if (!writeNamedAction(prog, 243, "АКБ выкл", "BATTERY_SAVER_OFF")) return false;
    if (!writeNamedAction(prog, 244, "Термо вкл", "THERMOSTAT_ON")) return false;
    if (!writeNamedAction(prog, 245, "Термо выкл", "THERMOSTAT_OFF")) return false;
    if (!writeInputTriggers(prog, 246, "Входы вкл", "INPUT_TRIGGER_ENABLE")) return false;
    if (!writeInputTriggers(prog, 247, "Входы выкл", "INPUT_TRIGGER_DISABLE")) return false;
    if (!writeTempTriggers(prog, 248, "Темп вкл", "TEMP_TRIGGER_ENABLE")) return false;
    if (!writeTempTriggers(prog, 249, "Темп выкл", "TEMP_TRIGGER_DISABLE")) return false;

    if (!rebuildProgramIndex()) {
        logger.log(LogModule::Config, "factory programs written, index not rebuilt\n");
        return false;
    }
    logger.log(LogModule::Config, "factory programs 1, 2 and 230-249 written\n");
    return true;
}

bool Config::loadProgramCompiled(uint8_t id, CompiledStep* outSteps, uint8_t maxSteps, uint8_t* outCount,
                                 char* outName, size_t outNameSize) {
    if (!outSteps || maxSteps == 0 || !outCount || !outName || outNameSize == 0) return false;

    char path[BufferBytes::Fs::PROGRAM_PATH];
    program_json::formatProgramPath(path, sizeof(path), id);

    File f = fileSystem.openRead(path);
    if (!f) {
        logger.log(LogModule::Config, "failed to read program %u\n", id);
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
    if (baseCache.thermostat.enabled) {
        if (baseCache.thermostat.program_id_lower == id || baseCache.thermostat.program_id_upper == id) return true;
    }
    if (baseCache.battery_saver.enabled && baseCache.battery_saver.program_id == id) return true;
    return false;
}

bool Config::deleteProgram(uint8_t id) {
    if (isProgramUsed(id)) {
        logger.log(LogModule::Config, "program %u in use, cannot delete\n", id);
        return false;
    }

    char path[BufferBytes::Fs::PROGRAM_PATH];
    program_json::formatProgramPath(path, sizeof(path), id);
    if (!fileSystem.deleteFile(path)) return false;

    if (!rebuildProgramIndex()) {
        logger.log(LogModule::Config, "program file %u deleted, index rebuild failed\n", id);
        return false;
    }
    logger.log(LogModule::Config, "program %u deleted\n", id);
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

    if (!writeFactoryPrograms()) ok = false;

    logger.log(LogModule::Config, "resetPrograms: %s\n", ok ? "ok" : "failed");
    return ok;
}
