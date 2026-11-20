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

    // Известные остатки временных файлов после прерванного приёма HTTP или промежуточной OTA.
    // Не обходить все *.tmp — атомарные временные файлы посреди записи должны дожить до rename.
    static const char* const kOrphans[] = {
        HttpPostJson::TMP_CONFIG,
        HttpPostJson::TMP_PROGRAM,
    };

    size_t removed = 0;
    for (const char* path : kOrphans) {
        if (!exists(path)) continue;
        if (deleteFile(path)) {
            ++removed;
            logger.log(LogModule::FsManager, "осиротевший удалён %s\n", path);
        }
    }
    return removed;
}

bool FsManager::gc() {
    if (!initialized) return false;

    // У ESP32 Arduino LittleFS нет LittleFS.gc(). Вернуть известные осиротевшие файлы, затем обновить свободное место.
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
                logger.log(LogModule::FsManager, "healthCheck восстановил /config.json из .bak\n");
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

void FsManager::printStats() {
#ifdef SERIAL_DEBUG
    fsInfo.totalBytes = LittleFS.totalBytes();
    fsInfo.usedBytes = LittleFS.usedBytes();
    logger.log(LogModule::FsManager, "=== статистика ===\n");
    logger.log(LogModule::FsManager, "всего: %u КБ\n", fsInfo.totalBytes / 1024);
    logger.log(LogModule::FsManager, "занято: %u КБ\n", fsInfo.usedBytes / 1024);
    logger.log(LogModule::FsManager, "свободно: %u КБ\n", (fsInfo.totalBytes - fsInfo.usedBytes) / 1024);
    logger.log(LogModule::FsManager, "чтений: %u\n", readCount);
    logger.log(LogModule::FsManager, "записей: %u\n", writeCount);
    logger.log(LogModule::FsManager, "обновлений сведений ФС: %u\n", fsInfoRefreshCount);
    logger.log(LogModule::FsManager, "ошибок: %u\n", errorCount);
    logger.log(LogModule::FsManager, "восстановлений: %u\n", recoveryCount);
    logger.log(LogModule::FsManager, "=== конец статистики ===\n");
#endif
}
