#include "fs/FSManager.h"
#include "common/Constants.h"
#include "common/Logger.h"
#include <WiFi.h>
#include <pgmspace.h>
#include <cstdio>
#include <sys/stat.h>

FSManager fileSystem;

static const FileMetadata fileRegistry[] PROGMEM = {
    // Критические файлы
    {
        "/config.json",
        FilePriority::PRIO_CRITICAL,
        FileAccess::ACCESS_READ_WRITE,
        Limits::CONFIG_JSON_SIZE,
        true,
        false,
        0
    },
    {
        "/config.bak",
        FilePriority::PRIO_LOW,
        FileAccess::ACCESS_READ_WRITE,
        Limits::CONFIG_JSON_SIZE,
        false,
        false,
        0
    },

    // Веб-интерфейс
    {
        "/index.html",
        FilePriority::PRIO_HIGH,
        FileAccess::ACCESS_READ_ONLY,
        FileMax::INDEX_HTML,
        false,
        true,
        0
    },
    {
        "/style.css",
        FilePriority::PRIO_HIGH,
        FileAccess::ACCESS_READ_ONLY,
        FileMax::STYLE_CSS,
        false,
        true,
        0
    },
    {
        "/app.js",
        FilePriority::PRIO_HIGH,
        FileAccess::ACCESS_READ_ONLY,
        FileMax::APP_JS,
        false,
        true,
        0
    },
    {
        "/favicon.ico",
        FilePriority::PRIO_NORMAL,
        FileAccess::ACCESS_READ_ONLY,
        FileMax::FAVICON_ICO,
        false,
        true,
        0
    },

    // Для OTA-обновлений
    {
        // OTA-пакет загружается через WebServer и обрабатывается OTAHandler.
        "/update.bin",
        FilePriority::PRIO_NORMAL,
        FileAccess::ACCESS_READ_WRITE,
        OTA::FILE_MAX_SIZE,
        false,
        false,
        0
    }
};

FSManager::FSManager() :
    initialized(false),
    lastFsInfoRefresh(0),
    readCount(0),
    writeCount(0),
    fsInfoRefreshCount(0),
    errorCount(0),
    recoveryCount(0),
    lastHealthCheck(0)
{
    memset(&fsInfo, 0, sizeof(FsSpaceInfo));
    memset(pathBuffer, 0, sizeof(pathBuffer));
}

bool FSManager::begin() {
    logger.log("[FSManager] begin\n");

    if (initialized) return true;

    if (LittleFS.begin()) {
        initialized = true;
        fsInfo.totalBytes = LittleFS.totalBytes();
        fsInfo.usedBytes = LittleFS.usedBytes();
        logger.log("[FSManager] LittleFS mounted, total: %u KB, used: %u KB\n",
                   fsInfo.totalBytes / 1024, fsInfo.usedBytes / 1024);
        if (!exists("/programs")) {
            LittleFS.mkdir("/programs");
#ifdef SERIAL_DEBUG
            logger.log("[FSManager] Created /programs directory\n");
#endif
        }
        // Создаём папки для библиотек, если их ещё нет (на всякий случай)
        if (!exists("/js")) {
            LittleFS.mkdir("/js");
#ifdef SERIAL_DEBUG
            logger.log("[FSManager] Created /js directory\n");
#endif
        }
        if (!exists("/css")) {
            LittleFS.mkdir("/css");
#ifdef SERIAL_DEBUG
            logger.log("[FSManager] Created /css directory\n");
#endif
        }
        logger.log("[FSManager] OK\n");
        return true;
    }

    logger.log("[FSManager] LittleFS mount failed, formatting...\n");
    if (!LittleFS.format()) {
        logger.log("[FSManager] LittleFS format FAILED\n");
        errorCount++;
        return false;
    }

    logger.log("[FSManager] LittleFS formatted, mounting again...\n");
    if (!LittleFS.begin()) {
        logger.log("[FSManager] LittleFS mount FAILED after format\n");
        errorCount++;
        return false;
    }

    initialized = true;
    fsInfo.totalBytes = LittleFS.totalBytes();
    fsInfo.usedBytes = LittleFS.usedBytes();
    if (!exists("/programs")) {
        LittleFS.mkdir("/programs");
    }
    if (!exists("/js")) {
        LittleFS.mkdir("/js");
    }
    if (!exists("/css")) {
        LittleFS.mkdir("/css");
    }
    logger.log("[FSManager] LittleFS mounted after format\n");
    logger.log("[FSManager] OK\n");
    return true;
}

void FSManager::end() {
    logger.log("[FSManager] Stopping...()\n");
    if (initialized) {
        LittleFS.end();
        initialized = false;
    }
}

bool FSManager::format() {
    logger.log("[FSManager] format()\n");
    bool result = LittleFS.format();
    if (result) {
        initialized = false;
    }
    return result;
}

bool FSManager::exists(const char* path) const {
    if (!initialized || !path || path[0] != '/') return false;
    // ESP32 Arduino exists() opens for read and logs E when missing
    // (vfs_api: "does not exist, no permits for creation"). Use POSIX stat instead.
    char full[BufferBytes::Fs::GZIP_PATH];
    const int n = snprintf(full, sizeof(full), "/littlefs%s", path);
    if (n <= 0 || (size_t)n >= sizeof(full)) return false;
    struct stat st;
    return (::stat(full, &st) == 0);
}

bool FSManager::rename(const char* oldPath, const char* newPath) {
    if (!initialized) return false;
    return LittleFS.rename(oldPath, newPath);
}

const FileMetadata* FSManager::getMetadata(const char* path) {
    for (size_t i = 0; i < sizeof(fileRegistry) / sizeof(FileMetadata); i++) {
        FileMetadata meta;
        memcpy_P(&meta, &fileRegistry[i], sizeof(FileMetadata));

        if (strcmp_P(path, meta.path) == 0) {
            return &fileRegistry[i];
        }
    }
    return nullptr;
}

bool FSManager::ensurePath(const char* path) {
    if (!initialized) return false;

    if (!path || path[0] == '\0') return true;

    // Ищем последний '/', не создавая String (лишние аллокации на ESP8266 нам не нужны).
    const char* lastSlash = strrchr(path, '/');
    if (!lastSlash || lastSlash == path) return true; // корень или нет родительской директории

    // Собираем путь к родителю в фиксированный буфер.
    char parent[BufferBytes::Fs::PATH];
    size_t len = (size_t)(lastSlash - path);
    if (len >= sizeof(parent)) len = sizeof(parent) - 1;
    memcpy(parent, path, len);
    parent[len] = '\0';

    if (parent[0] == '\0') return true;
    if (exists(parent)) return true;

    // Рекурсивно убеждаемся, что есть родитель родителя.
    if (!ensurePath(parent)) return false;

    bool created = LittleFS.mkdir(parent);
#ifdef SERIAL_DEBUG
    logger.log("[FSManager] %s directory: %s\n", created ? "Created" : "Failed to create", parent);
#else
    if (!created) {
        logger.log("[FSManager] Failed to create directory: %s\n", parent);
    }
#endif
    return created;
}

bool FSManager::hasRequiredWebAssets() const {
    if (!initialized) return false;

    // Axiom: UI shell assets on FS are only *.gz (logical names in WebAssets::REQUIRED).
    char path[BufferBytes::Fs::WEB_ASSET_PATH];
    char gzPath[BufferBytes::Fs::WEB_ASSET_GZIP_PATH];

    for (size_t i = 0; i < WebAssets::REQUIRED_COUNT; i++) {
        const char* ptr = WebAssets::REQUIRED[i];
        if (!ptr) return false;

        strlcpy(path, ptr, sizeof(path));
        if (path[0] == '\0') return false;

        strlcpy(gzPath, path, sizeof(gzPath));
        strlcat(gzPath, ".gz", sizeof(gzPath));
        if (!exists(gzPath)) return false;
    }

    return true;
}

void FSManager::forEachInDir(const char* path, DirVisitFn fn, void* ctx) {
    if (!initialized || !path || !fn) return;
    File root = LittleFS.open(path);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }
    File entry = root.openNextFile();
    while (entry) {
        const char* name = entry.name();
        if (name && name[0]) {
            fn(name, entry, ctx);
        }
        entry = root.openNextFile();
    }
    root.close();
}

