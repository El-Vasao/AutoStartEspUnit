// include/fs/FSManager.h
#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include "common/Constants.h"

/** ESP32 LittleFS space snapshot (replaces ESP8266 FSInfo). */
struct FsSpaceInfo {
    size_t totalBytes = 0;
    size_t usedBytes = 0;
};

// Приоритеты файлов (задел на будущее: можно использовать для политики GC/кеширования)
enum class FilePriority : uint8_t {
    PRIO_CRITICAL = 0,   ///< критичные (config.json)
    PRIO_HIGH = 1,       ///< веб-файлы
    PRIO_NORMAL = 2,     ///< временные файлы
    PRIO_LOW = 3         ///< логи, кэши
};

// Тип доступа к файлу
enum class FileAccess : uint8_t {
    ACCESS_READ_ONLY,    ///< только чтение (веб-морда)
    ACCESS_READ_WRITE,   ///< чтение и запись (конфиг)
    ACCESS_BOOT_CRITICAL ///< необходим для старта
};

// Метаданные файла (хранятся в PROGMEM)
// Примечание: это “реестр ожиданий” по файлам. Если добавляете новый важный файл (например для OTA),
// синхронизируйте путь здесь и в местах использования, иначе метаданные будут вводить в заблуждение.
struct FileMetadata {
    const char* path;               ///< путь к файлу
    FilePriority priority;           ///< приоритет
    FileAccess access;               ///< тип доступа
    uint32_t maxSize;                ///< максимальный размер (байт)
    bool cacheInRam;                 ///< подсказка для Config, не используется в FSManager
    bool gzipSupported;              ///< UI static: on FS only as path.gz (build.py axiom)
    uint16_t crc;                    ///< контрольная сумма (опционально)
} __attribute__((packed));

/**
 * @brief Менеджер файловой системы LittleFS.
 *
 * **Правило доступа к Flash:** любые операции с встроенной файловой системой (чтение, запись, удаление,
 * переименование, проверка существования, GC, mount) выполняются **только** через глобальный `fileSystem`
 * (методы этого класса). Не вызывайте `LittleFS.open` / `LittleFS.remove` / `LittleFS.exists` и т.п. из
 * остального кода — иначе обходятся учёт, атомарность и политика ошибок.
 *
 * **Исключение:** сторонние библиотеки иногда требуют ссылку на объект `FS` (например
 * `AsyncWebServer::beginResponse(FS&, path, …)`). В таких местах передавайте `fileSystem.webFs()` — это тот же
 * смонтированный том, но не используйте возвращаемый объект для `open`/`remove`; проверки и удаления по-прежнему
 * через `fileSystem`.
 *
 * Обеспечивает атомарную запись, бэкапы, сборку мусора и проверку целостности.
 *
 * Ключевые принципы (ESP8266/LittleFS):
 * - запись должна быть атомарной (через temp + rename), т.к. питание могут “выдёрнуть” в любой момент;
 * - нельзя заполнять том “в ноль” — LittleFS деградирует при нехватке свободных блоков;
 * - операции ввода/вывода должны быть короткими или периодически “yield()/feedWatchdog()” в вызывающем коде.
 *
 * Backup/restore/healthCheck копируют и считают CRC потоково; copyFileAtomic_ использует стековый scratch-буфер.
 */
class FSManager {
public:
    using JsonStreamEncodeFn = size_t (*)(Print& p, void* ctx);

    FSManager();

    /// Ссылка на смонтированный том для API, требующих `FS&` (см. комментарий к классу). Не вызывать на ней open/remove.
    FS& webFs() { return LittleFS; }

    // Монтировать файловую систему (с форматированием при ошибке)
    bool begin();

    // Размонтировать
    void end();

    // Отформатировать файловую систему
    bool format();

    // Проверить, инициализирована ли ФС
    bool isInitialized() const { return initialized; }

    // Прочитать весь файл в буфер
    bool readFile(const char* path, char* buffer, size_t& len, size_t maxLen);

    // Записать данные в файл атомарно (через .tmp рядом с целевым файлом).
    // При успехе — старый файл заменяется новым целиком (`writeFile` — тонкая обёртка над `atomicWrite`).
    bool writeFile(const char* path, const char* data, size_t len);

    /// Полный буфер в файл атомарно (tmp + rename). Общий путь для `writeFile`, флагов и т.п.
    bool atomicWrite(const char* path, const char* data, size_t len);

    // Дописать данные в конец файла
    bool appendFile(const char* path, const char* data, size_t len);

    // Удалить файл
    bool deleteFile(const char* path);

    // Проверить существование файла
    bool exists(const char* path) const;

    // Проверить, что web-ассеты присутствуют (иначе WebServer отдаёт fallback из PROGMEM)
    bool hasRequiredWebAssets() const;

    // Переименовать файл
    bool rename(const char* oldPath, const char* newPath);

    // Создать директорию
    bool mkdir(const char* path) { return initialized ? LittleFS.mkdir(path) : false; }

    /// Рекурсивно создать недостающие родительские каталоги для пути к файлу (перед записью).
    bool ensurePath(const char* path);

    // Удалить директорию
    bool rmdir(const char* path) { return initialized ? LittleFS.rmdir(path) : false; }

    // Открыть директорию для перечисления (ESP32 LittleFS: File + openNextFile).
    File openDir(const char* path) { return initialized ? LittleFS.open(path) : File(); }

    /// Перечислить файлы в каталоге (не рекурсивно). `name` — путь/имя записи.
    using DirVisitFn = void (*)(const char* name, File& entry, void* ctx);
    void forEachInDir(const char* path, DirVisitFn fn, void* ctx);

    // Создать резервную копию файла (path.bak)
    bool backup(const char* path);

    // Восстановить файл из резервной копии
    bool restore(const char* path);

    // Open UI static for web: FS holds only path.gz (logical `path` without suffix).
    File openWebFile(const char* path);

    // Дефрагментация (сборка мусора)
    bool gc();

    // Проверка целостности (восстановление из бэкапа при необходимости).
    // Важно: healthCheck — это “периодическая страховка” после неожиданных ребутов во время записи.
    bool healthCheck();

    // Получить свободное место (байт)
    size_t getFreeSpace();

    // Получить занятое место (байт)
    size_t getUsedSpace();

    // Статистика
    uint32_t getReadCount() const { return readCount; }
    uint32_t getWriteCount() const { return writeCount; }
    uint32_t getErrorCount() const { return errorCount; }
    uint32_t getRecoveryCount() const { return recoveryCount; }
    void printStats();
    const FsSpaceInfo& getInfo() const { return fsInfo; }

    // Потоковая запись (атомарная)
    File openWriteStream(const char* path, size_t expectedSize = 0);
    /// Потоковая запись по точному пути (без добавления второго «.tmp»). Для HTTP-ingest и др.;
    /// после записи достаточно File::close().
    File openDirectWrite(const char* path, size_t expectedMaxBytes = 0);
    File openRead(const char* path);
    bool closeWriteStream(File& f, const char* originalPath, bool commit = true);

    /** Атомарная запись UTF-8 JSON (tmp + rename), наполнение через колбэк `Print`. */
    bool writeJsonAtomicStream(const char* path, JsonStreamEncodeFn encoder, void* ctx, size_t maxBytes = Limits::CONFIG_JSON_SIZE);

private:
    static constexpr size_t TEMP_BUFFER_SIZE = PoolLimits::FS_SCRATCH_BYTES; ///< размер чанка copyFileAtomic_
    static constexpr size_t MAX_PATH_LEN = 32;

    bool initialized;               ///< флаг успешной инициализации
    FsSpaceInfo fsInfo;             ///< информация о ФС
    uint32_t lastGCTime;            ///< время последней сборки мусора

    uint32_t readCount;             ///< количество успешных чтений
    uint32_t writeCount;            ///< количество успешных записей
    uint32_t gcCount;               ///< количество запусков gc
    uint32_t errorCount;            ///< количество ошибок
    uint32_t recoveryCount;          ///< количество восстановлений из бэкапа
    uint32_t lastHealthCheck;        ///< время последней проверки здоровья

    // Ограничения по длине путей — сознательные: это “маленькое” устройство, и буфер пути живёт в объекте.
    // Если начнёте хранить более длинные пути — увеличьте MAX_PATH_LEN и проверьте все snprintf/strlcpy.
    char pathBuffer[MAX_PATH_LEN];   ///< буфер для путей

    /// Потоковая копия src → dest (через dest.tmp + rename), чанки по scratch-буферу.
    bool copyFileAtomic_(const char* srcPath, const char* destPath);

    // Создать бэкап
    bool createBackup(const char* path);

    // Получить метаданные файла из реестра
    const FileMetadata* getMetadata(const char* path);

};

extern FSManager fileSystem;

