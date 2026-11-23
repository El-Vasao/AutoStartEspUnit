#include "config/Config.h"
#include "common/EspHal.h"
#include "common/Fnv1a.h"
#include "fs/FsManager.h"
#include "common/Logger.h"
#include "common/Utils.h"
#include "config/internal/ProgramJsonIo.h"
#include "config/internal/ConfigStorageInternal.h"

#include <cstring>
#include <stdlib.h>

using namespace config_storage_internal;

namespace {

// Один буфер на ensure/rebuild/emit: два IndexRow[MAX_PROGRAMS] на стеке loopTask
// (≈3.3 КБ × 2) дают stack overflow при mismatch → rebuild.
program_json::IndexRow g_indexRows[Limits::MAX_PROGRAMS];

size_t encodeProgramIndexArray(Print& p, void* ctx) {
    struct Ctx {
        const program_json::IndexRow* rows;
        size_t n;
    };
    auto* box = reinterpret_cast<Ctx*>(ctx);
    return program_json::emitProgramIndexArrayPrint(box->rows, box->n, p);
}

bool loadRowsFromIndexFile(program_json::IndexRow* rows, size_t cap, size_t* outCount) {
    File f = fileSystem.openRead("/programs/index.json");
    if (!f) {
        return false;
    }
    const bool ok = program_json::parseProgramIndexFile(f, rows, cap, outCount);
    f.close();
    return ok;
}

} // namespace

void Config::refreshProgramsRevFromRows_(const void* rows, size_t n) {
    // Хеш сырых байт файлов программ в порядке возрастания id (строки уже отсортированы rebuild/ensure).
    Fnv1aPrint fp;
    const auto* items = static_cast<const program_json::IndexRow*>(rows);
    uint8_t chunk[PoolLimits::FS_SCRATCH_BYTES];
    for (size_t i = 0; i < n; i++) {
        char path[BufferBytes::Fs::PROGRAM_PATH];
        program_json::formatProgramPath(path, sizeof(path), items[i].id);
        File f = fileSystem.openRead(path);
        if (!f) continue;
        while (f.available()) {
            const size_t got = f.readBytes(reinterpret_cast<char*>(chunk), sizeof(chunk));
            if (!got) break;
            (void)fp.write(chunk, got);
            wdtPort_.feedNow();
        }
        f.close();
    }
    programsRev_ = fp.hash();
}

bool Config::emitProgramIndexArray(Print& p) const {
    size_t n = 0;
    File f = fileSystem.openRead("/programs/index.json");
    if (f) {
        const bool parsed = program_json::parseProgramIndexFile(f, g_indexRows, Limits::MAX_PROGRAMS, &n);
        f.close();
        if (!parsed) n = 0;
    }
    program_json::emitProgramIndexArrayPrint(g_indexRows, n, p);
    return true;
}

bool Config::ensureProgramIndex() {
    bool present[256] = { false };
    bool inIndex[256] = { false };
    size_t filesCount = 0;

    logger.log(LogModule::Config, "ensureProgramIndex: scanning /programs\n");
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

            const char* base = strrchr(fns, '/');
            base = base ? (base + 1) : fns;
            char* endptr = nullptr;
            long idInt = strtol(base, &endptr, 10);
            if (!endptr || strcmp(endptr, ".json") != 0) {
                entry = root.openNextFile();
                continue;
            }
            if (idInt <= 0 || idInt > 255) {
                entry = root.openNextFile();
                continue;
            }

            present[(uint8_t)idInt] = true;
            filesCount++;
            entry = root.openNextFile();
        }
        root.close();
    }

    bool mismatch = true;
    size_t indexCount = 0;

    {
        size_t n = 0;
        if (!loadRowsFromIndexFile(g_indexRows, Limits::MAX_PROGRAMS, &n)) {
            logger.log(LogModule::Config, "ensureProgramIndex: no valid index, rebuilding after JSON pool release\n");
            mismatch = true;
        } else {
            mismatch = false;
            indexCount = n;
            for (size_t i = 0; i < n; i++) {
                const uint8_t id = g_indexRows[i].id;
                if (id == 0 || id > 255) {
                    mismatch = true;
                    break;
                }
                inIndex[id] = true;
                if (!present[id]) {
                    mismatch = true;
                    break;
                }
            }

            if (!mismatch) {
                for (uint16_t pid = 1; pid <= 255; pid++) {
                    wdtPort_.feedNow();
                    wdtPort_.feedNow();
                    if (present[pid] && !inIndex[pid]) {
                        mismatch = true;
                        break;
                    }
                }
            }
        }
    }

    if (mismatch) {
        logger.log(LogModule::Config, "ensureProgramIndex: mismatch (files=%u, index=%u), rebuilding...\n",
                   (unsigned)filesCount, (unsigned)indexCount);
        // rebuild переиспользует g_indexRows; кадр ensure уже не держит свою копию.
        return rebuildProgramIndex();
    }

    refreshProgramsRevFromRows_(g_indexRows, indexCount);
    logger.log(LogModule::Config, "ensureProgramIndex: ok (files=%u index=%u)\n",
               (unsigned)filesCount, (unsigned)indexCount);
    return true;
}

bool Config::rebuildProgramIndex() {
    logger.log(LogModule::Config, "rebuildProgramIndex: building /programs/index.json\n");
    if (!ensureProgramsDir()) return false;

    size_t count = 0;

    logger.log(LogModule::Config, "rebuildProgramIndex: scanning /programs\n");
    espHalFeedWdt();
    File root = fileSystem.openDir("/programs");
    if (root) {
        File entry = root.openNextFile();
        while (entry) {
            wdtPort_.feedNow();
            wdtPort_.feedNow();
            char pathName[BufferBytes::Fs::PROGRAM_PATH];
            const char* rawName = entry.name();
            if (!rawName) {
                entry = root.openNextFile();
                continue;
            }
            strlcpy(pathName, rawName, sizeof(pathName));
            const char* fns = pathName;
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

            const char* base = strrchr(fns, '/');
            base = base ? (base + 1) : fns;
            char* endptr = nullptr;
            long idInt = strtol(base, &endptr, 10);
            if (!endptr || strcmp(endptr, ".json") != 0) {
                entry = root.openNextFile();
                continue;
            }
            if (idInt <= 0 || idInt > 255) {
                entry = root.openNextFile();
                continue;
            }
            const uint8_t id = static_cast<uint8_t>(idInt);

            uint8_t rid = 0;
            char name[sizeof program_json::IndexRow::name];
            name[0] = '\0';
            const bool okHdr = program_json::parseProgramHeaderFile(entry, id, &rid, name, sizeof name);
            entry.close();
            if (!okHdr) {
                logger.log(LogModule::Config, "rebuildProgramIndex: skip %s (header parse)\n", fns);
                entry = root.openNextFile();
                continue;
            }
            const uint8_t realId = rid ? rid : id;
            if (realId != id) {
                logger.log(LogModule::Config, "rebuildProgramIndex: skip %s (id mismatch %u!=%u)\n",
                           fns, (unsigned)realId, (unsigned)id);
                entry = root.openNextFile();
                continue;
            }

            if (count >= Limits::MAX_PROGRAMS) {
                logger.log(LogModule::Config, "rebuildProgramIndex: too many programs\n");
                break;
            }
            g_indexRows[count].id = id;
            strlcpy(g_indexRows[count].name, name, sizeof g_indexRows[count].name);
            count++;
            entry = root.openNextFile();
        }
        root.close();
    }

    espHalFeedWdt();

    for (size_t i = 0; i + 1 < count; i++) {
        wdtPort_.feedNow();
        wdtPort_.feedNow();
        for (size_t j = i + 1; j < count; j++) {
            if (g_indexRows[j].id < g_indexRows[i].id) {
                const program_json::IndexRow tmp = g_indexRows[i];
                g_indexRows[i] = g_indexRows[j];
                g_indexRows[j] = tmp;
            }
        }
    }

    espHalFeedWdt();

    struct Ctx {
        const program_json::IndexRow* rows;
        size_t n;
    } ctx{g_indexRows, count};

    const bool wrote =
        fileSystem.writeJsonAtomicStream("/programs/index.json", encodeProgramIndexArray, &ctx, Limits::CONFIG_JSON_SIZE);
    if (wrote) refreshProgramsRevFromRows_(g_indexRows, count);
    logger.log(LogModule::Config, "rebuildProgramIndex: %s (items: %u)\n", wrote ? "ok" : "failed", (unsigned)count);
    return wrote;
}
