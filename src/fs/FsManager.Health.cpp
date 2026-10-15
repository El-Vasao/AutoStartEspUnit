#include "fs/FSManager.h"
#include "common/Logger.h"
#include "common/Utils.h"
#include "common/EspHal.h"
#include "common/Constants.h"

#include <WiFi.h>

bool FSManager::refreshFsInfo() {
    if (!initialized) return false;

    espHalFeedWdt();
    fsInfo.totalBytes = LittleFS.totalBytes();
    fsInfo.usedBytes = LittleFS.usedBytes();
    lastFsInfoRefresh = millis();
    fsInfoRefreshCount++;
    return true;
}

size_t FSManager::reclaimOrphans() {
    if (!initialized) return 0;

    // Known leftover temps from interrupted HTTP ingest / OTA staging.
    // Do not walk all *.tmp — mid-write atomic temps must stay until rename.
    static const char* const kOrphans[] = {
        HttpPostJson::TMP_CONFIG,
        HttpPostJson::TMP_PROGRAM,
        "/update.bin.tmp",
    };

    size_t removed = 0;
    for (const char* path : kOrphans) {
        if (!exists(path)) continue;
        if (deleteFile(path)) {
            ++removed;
            logger.log("[FSManager] Reclaimed orphan %s\n", path);
        }
    }
    return removed;
}

bool FSManager::gc() {
    if (!initialized) return false;

    // ESP32 Arduino LittleFS has no LittleFS.gc(). Reclaim known orphans, then refresh free-space.
    reclaimOrphans();
    return refreshFsInfo();
}

bool FSManager::healthCheck() {
    if (!initialized) return false;

    lastHealthCheck = millis();

    if (!exists("/config.json")) {
        errorCount++;
        return false;
    }

    if (exists("/config.bak")) {
        uint16_t crc = 0;
        uint16_t bakCRC = 0;
        {
            File f = openRead("/config.json");
            if (f) {
                crc = crc16ModbusStreamFile(f);
                f.close();
            }
        }
        {
            File f = openRead("/config.bak");
            if (f) {
                bakCRC = crc16ModbusStreamFile(f);
                f.close();
            }
        }
        if (crc != bakCRC) {
            if (copyFileAtomic_("/config.bak", "/config.json")) {
                recoveryCount++;
            }
            errorCount++;
            return false;
        }
    }

    return true;
}

size_t FSManager::getFreeSpace() {
    if (!initialized) return 0;
    fsInfo.totalBytes = LittleFS.totalBytes();
    fsInfo.usedBytes = LittleFS.usedBytes();
    return fsInfo.totalBytes - fsInfo.usedBytes;
}

size_t FSManager::getUsedSpace() {
    if (!initialized) return 0;
    fsInfo.totalBytes = LittleFS.totalBytes();
    fsInfo.usedBytes = LittleFS.usedBytes();
    return fsInfo.usedBytes;
}

void FSManager::printStats() {
#ifdef SERIAL_DEBUG
    fsInfo.totalBytes = LittleFS.totalBytes();
    fsInfo.usedBytes = LittleFS.usedBytes();
    logger.log("[FSManager] === stats ===\n");
    logger.log("[FSManager] Total: %u KB\n", fsInfo.totalBytes / 1024);
    logger.log("[FSManager] Used: %u KB\n", fsInfo.usedBytes / 1024);
    logger.log("[FSManager] Free: %u KB\n", (fsInfo.totalBytes - fsInfo.usedBytes) / 1024);
    logger.log("[FSManager] Reads: %u\n", readCount);
    logger.log("[FSManager] Writes: %u\n", writeCount);
    logger.log("[FSManager] FS info refresh: %u\n", fsInfoRefreshCount);
    logger.log("[FSManager] Errors: %u\n", errorCount);
    logger.log("[FSManager] Recoveries: %u\n", recoveryCount);
    logger.log("[FSManager] === end stats ===\n");
#endif
}
