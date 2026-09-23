#include "fs/FSManager.h"
#include "core/Core.h"
#include "core/ErrorManager.h"
#include "common/Constants.h"
#include "common/ErrorCodes.h"
#include "common/EspHal.h"
#include "common/Logger.h"

#include <errno.h>

namespace {

void noteFsUnknown_() {
    auto& err = core.getErrorManager();
    if (err.get() == ErrorCode::NONE) {
        err.set(ErrorCode::FS_UNKNOWN);
    }
}

/// Длинная сериализация в File: без частого yield; кормим WDT каждые 256 записанных байт.
class JsonFilePrint final : public Print {
public:
    explicit JsonFilePrint(File& f) : file_(f) {}

    size_t write(uint8_t c) override {
        const size_t w = file_.write(c);
        if (w != 0) {
            totalBytes_ += static_cast<uint32_t>(w);
            if ((totalBytes_ & 0xFFu) == 0U) espHalFeedWdt();
        }
        return w;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        size_t done = 0;
        while (done < size) {
            size_t chunk = size - done;
            if (chunk > 128) chunk = 128;
            const size_t w = file_.write(buffer + done, chunk);
            if (w == 0) break;
            done += w;
            totalBytes_ += w;
            if ((totalBytes_ & 0xFFu) == 0U) espHalFeedWdt();
        }
        return done;
    }

private:
    File& file_;
    uint32_t totalBytes_{0};
};

static bool commitTmpToPath_atomic_(const char* path, char* tmpPath, bool hadBackupBackup) {
    (void)hadBackupBackup;
    char bakPath[BufferBytes::Fs::PATH];
    snprintf(bakPath, sizeof(bakPath), "%s.bak", path);
    bakPath[sizeof(bakPath) - 1] = '\0';

    bool hadBackup = false;
    if (fileSystem.exists(path)) {
        if (fileSystem.exists(bakPath)) {
            LittleFS.remove(bakPath);
        }
        hadBackup = LittleFS.rename(path, bakPath);
        if (!hadBackup) {
            if (!LittleFS.remove(path)) {
                logger.log("[FSManager] atomic stream: cannot remove target %s (errno=%d)\n", path, errno);
                LittleFS.remove(tmpPath);
                return false;
            }
        }
    }

    espHalFeedWdt();
    if (!LittleFS.rename(tmpPath, path)) {
        logger.log("[FSManager] atomic stream: rename failed (errno=%d)\n", errno);
        LittleFS.remove(tmpPath);
        if (hadBackup) {
            (void)LittleFS.rename(bakPath, path);
        }
        noteFsUnknown_();
        return false;
    }

    if (hadBackup) {
        LittleFS.remove(bakPath);
    }

    espHalFeedWdt();
    return true;
}

} // namespace

bool FSManager::writeJsonAtomicStream(const char* path, JsonStreamEncodeFn encoder, void* ctx,
                                     size_t maxBytes) {
    if (!initialized || !path || path[0] != '/' || !encoder || maxBytes == 0) {
        return false;
    }

    if (!ensurePath(path)) {
        logger.log("[FSManager] writeJsonAtomicStream: cannot ensure path %s\n", path);
        errorCount++;
        return false;
    }

    char tmpPath[BufferBytes::Fs::PATH];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    tmpPath[sizeof(tmpPath) - 1] = '\0';

    if (fileSystem.exists(tmpPath)) {
        LittleFS.remove(tmpPath);
    }

    File f = LittleFS.open(tmpPath, "w");
    if (!f) {
        logger.log("[FSManager] writeJsonAtomicStream: cannot create temp %s\n", tmpPath);
        errorCount++;
        return false;
    }

    JsonFilePrint jfp(f);
    const size_t written = encoder(jfp, ctx);
    espHalFeedWdt();
    f.close();

    if (written == 0 || written >= maxBytes) {
        logger.log("[FSManager] writeJsonAtomicStream: bad byte count (%u)\n", (unsigned)written);
        LittleFS.remove(tmpPath);
        errorCount++;
        return false;
    }

    if (!commitTmpToPath_atomic_(path, tmpPath, false)) {
        errorCount++;
        return false;
    }

    writeCount++;
    return true;
}
