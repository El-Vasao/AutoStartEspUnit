// include/config/internal/ProgramJsonIo.h — реализация: src/config/Config.ProgramJsonIo.cpp
#pragma once

#include <Arduino.h>
#include <FS.h>

#include "config/ConfigTypes.h"
#include "program/CompiledStep.h"

namespace program_json {

struct IndexRow {
    uint8_t id;
    char name[TextBytes::Programs::NAME];
};

/** Top-level `[{id,name},...]` from `/programs/index.json`. */
bool parseProgramIndexFile(File& f, IndexRow* rows, size_t rowCap, size_t* outCount);

bool parseProgramObjectFile(File& f, Program& out);

bool parseProgramCompiledFile(File& f, uint8_t expectedId, CompiledStep* outSteps, uint8_t maxSteps,
                              uint8_t* outCount, char* outName, size_t outNameSize);

/** Read `id` + `name` only; skips `steps` subtree. Validates `pathId` vs `id` like rebuild. */
bool parseProgramHeaderFile(File& f, uint8_t pathId, uint8_t* outId, char* outName, size_t outNameSize);

size_t emitProgramToPrint(const Program& p, Print& out);

size_t emitProgramIndexArrayPrint(const IndexRow* rows, size_t n, Print& out);

/** `{"programs":[...same as emitProgramIndexArray...]}`. */
size_t emitProgramListWrappedPrint(const IndexRow* rows, size_t n, Print& out);

} // namespace program_json
