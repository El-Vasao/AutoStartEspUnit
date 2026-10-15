#include "fs/FsManager.h"
#include "common/Logger.h"
#include "common/Utils.h"
#include "common/EspHal.h"
#include "common/Constants.h"

#include <WiFi.h>

bool FsManager::refreshFsInfo() {
    if (!initialized) return false;

    espHalFeedWdt();
    fsInfo.totalBytes = LittleFS.totalBytes();
    fsInfo.usedBytes = LittleFS.usedBytes();
    lastFsInfoRefresh = millis();
    fsInfoRefreshCount++;
    return true;
}

size_t FsManager::reclaimOrphans() {
    if (!initialized) return 0;

    // Known leftover temps from interrupted HTTP ingest / OTA staging.
    // Do not walk all *.tmp — mid-write atomic temps must stay until rename.
    static const char* const kOrphans[] = {
        HttpPostJson::TMP_CONFIG,
        HttpPostJson::TMP_PROGRAM,
    };

    size_t removed = 0;
    for (const char* path : kOrphans) {
        if (!exists(path)) continue;
        if (deleteFile(path)) {
            ++removed;
            logger.log(LogModule::FsManager, "Reclaimed orphan %s\n", path);
        }
    }
    return removed;
}

bool FsManager::gc() {
    if (!initialized) return false;

    // ESP32 Arduino LittleFS has no LittleFS.gc(). Reclaim known orphans, then refresh free-space.
    reclaimOrphans();
    return refreshFsInfo();
}

bool FsManager::healthCheck() {
    if (!initialized) return false;

    lastHealthCheck = millis();

    if (!exists("/config.json")) {
        errorCount++;
        return false;
    }

    {
        File f = openRead("/config.json");
        const bool unreadable = !f || f.size() == 0;
        if (f) f.close();
        if (unreadable && exists("/config.bak")) {
            if (copyFileAtomic_("/config.bak", "/config.json")) {
                recoveryCount++;
                logger.log(LogModule::FsManager, "healthCheck restored /config.json from .bak\n");
            } else {
                errorCount++;
                return false;
            }
        }
    }

    return true;
}

size_t FsManager::getFreeSpace() {
    if (!initialized) return 0;
    fsInfo.totalBytes = LittleFS.totalBytes();
    fsInfo.usedBytes = LittleFS.usedBytes();
    return fsInfo.totalBytes - fsInfo.usedBytes;
}

size_t FsManager::getUsedSpace() {
    if (!initialized) return 0;
    fsInfo.totalBytes = LittleFS.totalBytes();
    fsInfo.usedBytes = LittleFS.usedBytes();
    return fsInfo.usedBytes;
}

void FsManager::printStats() {
#ifdef SERIAL_DEBUG
    fsInfo.totalBytes = LittleFS.totalBytes();
    fsInfo.usedBytes = LittleFS.usedBytes();
    logger.log(LogModule::FsManager, "=== stats ===\n");
    logger.log(LogModule::FsManager, "Total: %u KB\n", fsInfo.totalBytes / 1024);
    logger.log(LogModule::FsManager, "Used: %u KB\n", fsInfo.usedBytes / 1024);
    logger.log(LogModule::FsManager, "Free: %u KB\n", (fsInfo.totalBytes - fsInfo.usedBytes) / 1024);
    logger.log(LogModule::FsManager, "Reads: %u\n", readCount);
    logger.log(LogModule::FsManager, "Writes: %u\n", writeCount);
    logger.log(LogModule::FsManager, "FS info refresh: %u\n", fsInfoRefreshCount);
    logger.log(LogModule::FsManager, "Errors: %u\n", errorCount);
    logger.log(LogModule::FsManager, "Recoveries: %u\n", recoveryCount);
    logger.log(LogModule::FsManager, "=== end stats ===\n");
#endif
}
