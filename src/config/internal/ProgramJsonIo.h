// src/config/internal/ProgramJsonIo.h — реализация: src/config/Config.ProgramJsonIo.cpp
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

/** `/programs/<id>.json` в буфер размера `BufferBytes::Fs::PROGRAM_PATH`. */
inline void formatProgramPath(char* out, size_t outSz, uint8_t id) {
    if (!out || outSz == 0) return;
    snprintf(out, outSz, "/programs/%u.json", (unsigned)id);
    out[outSz - 1] = '\0';
}

/** Верхний уровень `[{id,name},...]` из `/programs/index.json`. */
bool parseProgramIndexFile(File& f, IndexRow* rows, size_t rowCap, size_t* outCount);

bool parseProgramObjectFile(File& f, Program& out);

bool parseProgramCompiledFile(File& f, uint8_t expectedId, CompiledStep* outSteps, uint8_t maxSteps,
                              uint8_t* outCount, char* outName, size_t outNameSize);

/** Читает только `id` и `name`; пропускает поддерево `steps`. Сверяет `pathId` с `id`, как при пересборке. */
bool parseProgramHeaderFile(File& f, uint8_t pathId, uint8_t* outId, char* outName, size_t outNameSize);

size_t emitProgramToPrint(const Program& p, Print& out);

size_t emitProgramIndexArrayPrint(const IndexRow* rows, size_t n, Print& out);

} // namespace program_json
