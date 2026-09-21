#include "fs/FSManager.h"
#include "core/Core.h"
#include "common/Constants.h"
#include "common/EspHal.h"
#include "common/Logger.h"

#include <WiFi.h>

bool FSManager::readFile(const char* path, char* buffer, size_t& len, size_t maxLen) {
#ifdef SERIAL_DEBUG
    logger.log("[FSManager] readFile(%s)\n", path);
#endif

    if (!initialized) {
        logger.log("[FSManager]   FS not initialized\n");
        errorCount++;
        return false;
    }
    if (!buffer || maxLen == 0) {
        logger.log("[FSManager]   Invalid buffer\n");
        errorCount++;
        return false;
    }

    len = 0;

    auto meta = getMetadata(path);
    if (meta) {
        FileMetadata m;
        memcpy_P(&m, meta, sizeof(FileMetadata));
        if (maxLen > m.maxSize) {
            maxLen = m.maxSize;
        }
    }

    File f = LittleFS.open(path, "r");
    if (!f) {
        logger.log("[FSManager]   Failed to open file\n");
        errorCount++;
        return false;
    }

    size_t read = f.readBytes(buffer, maxLen);
    f.close();

    if (read > 0) {
        len = read;
        readCount++;
        return true;
    }

    logger.log("[FSManager]   File is empty\n");
    errorCount++;
    return false;
}

bool FSManager::atomicWrite(const char* path, const char* data, size_t len) {
#ifdef SERIAL_DEBUG
    logger.log("[FSManager] atomicWrite(%s, %u bytes)\n", path, len);
#endif

    auto meta = getMetadata(path);
    if (meta) {
        FileMetadata m;
        memcpy_P(&m, meta, sizeof(FileMetadata));
        if (len > m.maxSize) {
            logger.log("[FSManager] Exceeds maxSize (%u > %u) for %s\n", (unsigned)len, (unsigned)m.maxSize, path);
            errorCount++;
            return false;
        }
    }

    if (!ensurePath(path)) {
        logger.log("[FSManager] Failed to ensure parent directory for %s\n", path);
        errorCount++;
        return false;
    }

    fsInfo.totalBytes = LittleFS.totalBytes();
    fsInfo.usedBytes = LittleFS.usedBytes();
    if (fsInfo.usedBytes + len + FSystem::GC_SPACE_MARGIN > fsInfo.totalBytes) {
        logger.log("[FSManager] Low space before atomicWrite, reclaim orphans…\n");
        gc();
        espHalFeedWdt();
    }

    strlcpy(pathBuffer, path, sizeof(pathBuffer));
    strlcat(pathBuffer, ".tmp", sizeof(pathBuffer));

    LittleFS.remove(pathBuffer);
    espHalFeedWdt();

    File f = LittleFS.open(pathBuffer, "w");
    if (!f) {
        logger.log("[FSManager] Failed to create temp file %s\n", pathBuffer);
        errorCount++;
        return false;
    }

    size_t written = f.write((const uint8_t*)data, len);
    f.close();
    espHalFeedWdt();

    if (written != len) {
        logger.log("[FSManager] atomicWrite short write %s (%u/%u)\n", pathBuffer, (unsigned)written, (unsigned)len);
        LittleFS.remove(pathBuffer);
        errorCount++;
        return false;
    }

    LittleFS.remove(path);
    espHalFeedWdt();

    if (!LittleFS.rename(pathBuffer, path)) {
        logger.log("[FSManager] atomicWrite rename failed %s -> %s\n", pathBuffer, path);
        LittleFS.remove(pathBuffer);
        errorCount++;
        return false;
    }

    espHalFeedWdt();
    writeCount++;
    return true;
}

bool FSManager::copyFileAtomic_(const char* srcPath, const char* destPath) {
    if (!initialized) return false;

    File src = LittleFS.open(srcPath, "r");
    if (!src) {
        errorCount++;
        return false;
    }
    const size_t total = src.size();
    if (total == 0) {
        src.close();
        errorCount++;
        return false;
    }

    auto meta = getMetadata(destPath);
    if (meta) {
        FileMetadata m;
        memcpy_P(&m, meta, sizeof(FileMetadata));
        if (total > m.maxSize) {
            logger.log("[FSManager] copyFileAtomic_: size %u > max %u for %s\n",
                       (unsigned)total, (unsigned)m.maxSize, destPath);
            src.close();
            errorCount++;
            return false;
        }
    }

    if (!ensurePath(destPath)) {
        src.close();
        return false;
    }

    char tmpPath[BufferBytes::Fs::PATH];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", destPath);
    tmpPath[sizeof(tmpPath) - 1] = '\0';

    if (exists(tmpPath)) {
        LittleFS.remove(tmpPath);
    }

    File dst = LittleFS.open(tmpPath, "w");
    if (!dst) {
        src.close();
        errorCount++;
        return false;
    }

    uint8_t chunk[PoolLimits::FS_SCRATCH_BYTES];

    size_t writtenTotal = 0;
    while (src.available()) {
        const size_t n = src.readBytes(reinterpret_cast<char*>(chunk), PoolLimits::FS_SCRATCH_BYTES);
        if (n == 0) break;
        if (dst.write(chunk, n) != n) {
            src.close();
            dst.close();
            LittleFS.remove(tmpPath);
            errorCount++;
            return false;
        }
        writtenTotal += n;
        espHalFeedWdt();
        core.cooperate();
    }
    src.close();
    dst.close();

    if (writtenTotal != total) {
        LittleFS.remove(tmpPath);
        errorCount++;
        return false;
    }

    if (exists(destPath)) {
        if (!LittleFS.remove(destPath)) {
            LittleFS.remove(tmpPath);
            errorCount++;
            return false;
        }
    }
    if (!LittleFS.rename(tmpPath, destPath)) {
        LittleFS.remove(tmpPath);
        errorCount++;
        return false;
    }

    writeCount++;
    return true;
}

bool FSManager::writeFile(const char* path, const char* data, size_t len) {
#ifdef SERIAL_DEBUG
    logger.log("[FSManager] writeFile(%s, %u bytes)\n", path, len);
#endif

    if (!initialized) {
        logger.log("[FSManager] writeFile: FS not initialized\n");
        errorCount++;
        return false;
    }

    return atomicWrite(path, data, len);
}

bool FSManager::appendFile(const char* path, const char* data, size_t len) {
    if (!initialized) return false;

    auto meta = getMetadata(path);
    if (meta) {
        FileMetadata m;
        memcpy_P(&m, meta, sizeof(FileMetadata));
        if (m.access == FileAccess::ACCESS_READ_ONLY) {
            return false;
        }
    }

    File f = LittleFS.open(path, "a");
    if (!f) return false;

    size_t written = f.write((const uint8_t*)data, len);
    f.close();

    return written == len;
}

bool FSManager::deleteFile(const char* path) {
    if (!initialized) return false;

    auto meta = getMetadata(path);
    if (meta) {
        FileMetadata m;
        memcpy_P(&m, meta, sizeof(FileMetadata));
        if (m.access == FileAccess::ACCESS_READ_ONLY) {
            return false;
        }
    }

    return LittleFS.remove(path);
}

bool FSManager::createBackup(const char* path) {
    strlcpy(pathBuffer, path, sizeof(pathBuffer));
    strlcat(pathBuffer, ".bak", sizeof(pathBuffer));
    return copyFileAtomic_(path, pathBuffer);
}

bool FSManager::backup(const char* path) {
    return createBackup(path);
}

bool FSManager::restore(const char* path) {
    strlcpy(pathBuffer, path, sizeof(pathBuffer));
    strlcat(pathBuffer, ".bak", sizeof(pathBuffer));
    return copyFileAtomic_(pathBuffer, path);
}

File FSManager::openWebFile(const char* path) {
    if (!initialized) return File();

    auto meta = getMetadata(path);
    if (!meta) return File();

    FileMetadata m;
    memcpy_P(&m, meta, sizeof(FileMetadata));

    if (m.access != FileAccess::ACCESS_READ_ONLY) {
        return File();
    }

    // Axiom: UI statics on FS are only *.gz.
    if (!m.gzipSupported) {
        return File();
    }

    strlcpy(pathBuffer, path, sizeof(pathBuffer));
    strlcat(pathBuffer, ".gz", sizeof(pathBuffer));
    if (!exists(pathBuffer)) {
        return File();
    }
    return LittleFS.open(pathBuffer, "r");
}

File FSManager::openWriteStream(const char* path, size_t expectedSize) {
    if (!initialized) {
        logger.log("[FSManager] openWriteStream: FS not initialized\n");
        errorCount++;
        return File();
    }

    // Убедимся, что родительская директория существует
    if (!ensurePath(path)) {
        logger.log("[FSManager] openWriteStream: cannot create parent directory for %s\n", path);
        errorCount++;
        return File();
    }

    size_t freeSpace = getFreeSpace();
#ifdef SERIAL_DEBUG
    logger.log("[FSManager] openWriteStream: free=%u, expected=%u\n", (unsigned)freeSpace, (unsigned)expectedSize);
#endif
    if (freeSpace < expectedSize + FSystem::STREAM_SPACE_MARGIN) {
        logger.log("[FSManager] Low free space, reclaiming orphans…\n");
        gc();
        espHalFeedWdt();
        freeSpace = getFreeSpace();
        if (freeSpace < expectedSize + FSystem::STREAM_SPACE_MARGIN) {
            logger.log("[FSManager] Still low space, aborting\n");
            errorCount++;
            return File();
        }
    }

    // Временный файл создаём в той же директории, что и целевой
    strlcpy(pathBuffer, path, sizeof(pathBuffer));
    strlcat(pathBuffer, ".tmp", sizeof(pathBuffer));

    if (exists(pathBuffer)) {
        LittleFS.remove(pathBuffer);
    }

    espHalFeedWdt();

    File f = LittleFS.open(pathBuffer, "w");
    if (!f) {
        logger.log("[FSManager] openWriteStream: failed to open %s\n", pathBuffer);
        errorCount++;
        return File();
    }

#ifdef SERIAL_DEBUG
    logger.log("[FSManager] openWriteStream: created %s\n", pathBuffer);
#endif
    return f;
}

File FSManager::openDirectWrite(const char* path, size_t expectedMaxBytes) {
    if (!initialized) {
        logger.log("[FSManager] openDirectWrite: FS not initialized\n");
        errorCount++;
        return File();
    }
    if (!path || !path[0]) {
        errorCount++;
        return File();
    }
    if (!ensurePath(path)) {
        logger.log("[FSManager] openDirectWrite: cannot create parent directory for %s\n", path);
        errorCount++;
        return File();
    }

    size_t freeSpace = getFreeSpace();
    if (freeSpace < expectedMaxBytes + FSystem::STREAM_SPACE_MARGIN) {
        logger.log("[FSManager] openDirectWrite: low free space, reclaiming orphans…\n");
        gc();
        freeSpace = getFreeSpace();
        if (freeSpace < expectedMaxBytes + FSystem::STREAM_SPACE_MARGIN) {
            logger.log("[FSManager] openDirectWrite: still low space, abort\n");
            errorCount++;
            return File();
        }
    }

    if (exists(path)) {
        LittleFS.remove(path);
    }

    File f = LittleFS.open(path, "w");
    if (!f) {
        logger.log("[FSManager] openDirectWrite: failed to open %s\n", path);
        errorCount++;
        return File();
    }
    logger.log("[FSManager] openDirectWrite: opened %s\n", path);
    return f;
}

File FSManager::openRead(const char* path) {
    if (!initialized) return File();
    return LittleFS.open(path, "r");
}

bool FSManager::closeWriteStream(File& f, const char* originalPath, bool commit) {
    if (!f) {
        errorCount++;
        return false;
    }

    char tempPath[BufferBytes::Fs::TEMP_PATH];
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", originalPath ? originalPath : "");
    tempPath[sizeof(tempPath) - 1] = '\0';
    f.close();
    espHalFeedWdt();

    if (!commit) {
        bool removed = LittleFS.remove(tempPath);
#ifdef SERIAL_DEBUG
        logger.log("[FSManager] closeWriteStream: temp %s removed: %d\n", tempPath, (int)removed);
#endif
        return removed;
    }

    const char* targetPath = originalPath;

#ifdef SERIAL_DEBUG
    logger.log("[FSManager] closeWriteStream: temp=%s, target=%s\n", tempPath, targetPath ? targetPath : "");
#endif

    if (targetPath && exists(targetPath)) {
        if (!LittleFS.remove(targetPath)) {
            logger.log("[FSManager] closeWriteStream: failed to remove target %s\n", targetPath);
            LittleFS.remove(tempPath);
            errorCount++;
            return false;
        }
        espHalFeedWdt();
    }

    if (!targetPath || !LittleFS.rename(tempPath, targetPath)) {
        logger.log("[FSManager] closeWriteStream: rename FAILED %s\n", tempPath);
        LittleFS.remove(tempPath);
        errorCount++;
        return false;
    }

    espHalFeedWdt();
#ifdef SERIAL_DEBUG
    logger.log("[FSManager] closeWriteStream: rename OK\n");
#endif
    writeCount++;
    return true;
}

