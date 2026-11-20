#include "fs/FsManager.h"
#include "common/Constants.h"
#include "common/Logger.h"
#include <WiFi.h>
#include <pgmspace.h>
#include <cstdio>
#include <sys/stat.h>

FsManager fileSystem;

static const FileMetadata fileRegistry[] PROGMEM = {
    // Критические файлы
    {
        "/config.json",
        FileAccess::ACCESS_READ_WRITE,
        Limits::CONFIG_JSON_SIZE,
    },
    {
        "/config.bak",
        FileAccess::ACCESS_READ_WRITE,
        Limits::CONFIG_JSON_SIZE,
    },

    // Веб-интерфейс
    {
        "/index.html",
        FileAccess::ACCESS_READ_ONLY,
        FileMax::INDEX_HTML,
    },
    {
        "/style.css",
        FileAccess::ACCESS_READ_ONLY,
        FileMax::STYLE_CSS,
    },
    {
        "/app.js",
        FileAccess::ACCESS_READ_ONLY,
        FileMax::APP_JS,
    },
    {
        "/favicon.ico",
        FileAccess::ACCESS_READ_ONLY,
        FileMax::FAVICON_ICO,
    },

};

FsManager::FsManager() :
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

bool FsManager::begin() {
    logger.log(LogModule::FsManager, "start\n");

    if (initialized) return true;

    if (LittleFS.begin()) {
        initialized = true;
        fsInfo.totalBytes = LittleFS.totalBytes();
        fsInfo.usedBytes = LittleFS.usedBytes();
        logger.log(LogModule::FsManager, "LittleFS mounted, total: %u KB, used: %u KB\n",
                   fsInfo.totalBytes / 1024, fsInfo.usedBytes / 1024);
        if (!exists("/programs")) {
            LittleFS.mkdir("/programs");
#ifdef SERIAL_DEBUG
            logger.log(LogModule::FsManager, "dir /programs created\n");
#endif
        }
        // Создаём папки для библиотек, если их ещё нет (на всякий случай)
        if (!exists("/js")) {
            LittleFS.mkdir("/js");
#ifdef SERIAL_DEBUG
            logger.log(LogModule::FsManager, "dir /js created\n");
#endif
        }
        if (!exists("/css")) {
            LittleFS.mkdir("/css");
#ifdef SERIAL_DEBUG
            logger.log(LogModule::FsManager, "dir /css created\n");
#endif
        }
        logger.log(LogModule::FsManager, "ok\n");
        return true;
    }

    logger.log(LogModule::FsManager, "LittleFS mount failed, formatting...\n");
    if (!LittleFS.format()) {
        logger.log(LogModule::FsManager, "LittleFS format FAILED\n");
        errorCount++;
        return false;
    }

    logger.log(LogModule::FsManager, "LittleFS formatted, remounting...\n");
    if (!LittleFS.begin()) {
        logger.log(LogModule::FsManager, "LittleFS mount FAILED after formatting\n");
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
    logger.log(LogModule::FsManager, "LittleFS mounted after formatting\n");
    logger.log(LogModule::FsManager, "ok\n");
    return true;
}

void FsManager::end() {
    logger.log(LogModule::FsManager, "stopping...()\n");
    if (initialized) {
        LittleFS.end();
        initialized = false;
    }
}

bool FsManager::format() {
    logger.log(LogModule::FsManager, "calling format()\n");
    bool result = LittleFS.format();
    if (result) {
        initialized = false;
    }
    return result;
}

bool FsManager::exists(const char* path) const {
    if (!initialized || !path || path[0] != '/') return false;
#if defined(UNIT_TEST)
    // Хост: тома /littlefs нет, реестр файлов — RAM-заглушка LittleFS.
    return LittleFS.exists(path);
#else
    // ESP32 Arduino exists() открывает файл на чтение и пишет E, если его нет
    // (vfs_api: "does not exist, no permits for creation"). Вместо этого POSIX stat.
    char full[BufferBytes::Fs::GZIP_PATH];
    const int n = snprintf(full, sizeof(full), "/littlefs%s", path);
    if (n <= 0 || (size_t)n >= sizeof(full)) return false;
    struct stat st;
    return (::stat(full, &st) == 0);
#endif
}

bool FsManager::rename(const char* oldPath, const char* newPath) {
    if (!initialized) return false;
    return LittleFS.rename(oldPath, newPath);
}

const FileMetadata* FsManager::getMetadata(const char* path) {
    for (size_t i = 0; i < sizeof(fileRegistry) / sizeof(FileMetadata); i++) {
        FileMetadata meta;
        memcpy_P(&meta, &fileRegistry[i], sizeof(FileMetadata));

        if (strcmp_P(path, meta.path) == 0) {
            return &fileRegistry[i];
        }
    }
    return nullptr;
}

bool FsManager::ensurePath(const char* path) {
    if (!initialized) return false;

    if (!path || path[0] == '\0') return true;

    // Ищем последний '/', не создавая String.
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
    logger.log(LogModule::FsManager, "%s dir: %s\n", created ? "created" : "not created", parent);
#else
    if (!created) {
        logger.log(LogModule::FsManager, "failed to create dir: %s\n", parent);
    }
#endif
    return created;
}

bool FsManager::hasRequiredWebAssets() const {
    if (!initialized) return false;

    // Аксиома: файлы оболочки UI на ФС только *.gz (логические имена в WebAssets::REQUIRED).
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

