// include/fs/FsManager.h
#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include "common/Constants.h"

/** Снимок места ESP32 LittleFS. */
struct FsSpaceInfo {
    size_t totalBytes = 0;
    size_t usedBytes = 0;
};

// Тип доступа к файлу
enum class FileAccess : uint8_t {
    ACCESS_READ_ONLY,    ///< только чтение (веб-морда)
    ACCESS_READ_WRITE    ///< чтение и запись (конфиг)
};

// Метаданные файла (хранятся в PROGMEM)
// Примечание: это “реестр ожиданий” по файлам. Если добавляете новый важный файл (например для OTA),
// синхронизируйте путь здесь и в местах использования, иначе метаданные будут вводить в заблуждение.
struct FileMetadata {
    const char* path;               ///< путь к файлу
    FileAccess access;               ///< тип доступа
    uint32_t maxSize;                ///< максимальный размер (байт)
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
 * Обеспечивает атомарную запись, бэкапы, reclaim orphans и проверку целостности.
 *
 * Ключевые принципы (ESP32-C3 / LittleFS):
 * - запись должна быть атомарной (через temp + rename), т.к. питание могут “выдёрнуть” в любой момент;
 * - нельзя заполнять том “в ноль” — LittleFS деградирует при нехватке свободных блоков;
 * - операции ввода/вывода должны быть короткими или периодически “cooperate()/yield()” в вызывающем коде.
 * - `gc()` на ESP32 Arduino не компактирует том: только возврат осиротевших файлов и обновление свободного места.
 *
 * Backup/restore/healthCheck копируют и считают CRC потоково; copyFileAtomic_ использует стековый scratch-буфер.
 */
class FsManager {
public:
    using JsonStreamEncodeFn = size_t (*)(Print& p, void* ctx);

    FsManager();

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

    /// Полный буфер в файл атомарно (tmp + rename).
    bool atomicWrite(const char* path, const char* data, size_t len);

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

    /// Обновить снимок total/used (ESP32 LittleFS не даёт настоящий GC).
    bool refreshFsInfo();

    /// Удалить известные осиротевшие временные файлы (остатки приёма HTTP и промежуточной OTA).
    size_t reclaimOrphans();

    /// Возврат осиротевших файлов и refreshFsInfo. Не компактирует том — после вызова всегда проверяйте свободное место.
    bool gc();

    // Проверка целостности (восстановление из бэкапа при необходимости).
    // Важно: healthCheck — это “периодическая страховка” после неожиданных ребутов во время записи.
    bool healthCheck();

    // Получить свободное место (байт). Ходит в LittleFS — только с цикла.
    size_t getFreeSpace();

    /// total−used из последнего снимка, без LittleFS. Можно из задачи async.
    size_t cachedFreeSpace() const {
        if (!initialized || fsInfo.totalBytes < fsInfo.usedBytes) return 0;
        return fsInfo.totalBytes - fsInfo.usedBytes;
    }

    // Статистика
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
    /// Должен вмещать самый длинный путь ФС плюс ".tmp" (например /validation/settings.schema.json.tmp).
    static constexpr size_t MAX_PATH_LEN = BufferBytes::Fs::TEMP_PATH;

    bool initialized;               ///< флаг успешной инициализации
    FsSpaceInfo fsInfo;             ///< информация о ФС
    uint32_t lastFsInfoRefresh;     ///< время последнего refreshFsInfo/gc

    uint32_t readCount;             ///< количество успешных чтений
    uint32_t writeCount;            ///< количество успешных записей
    uint32_t fsInfoRefreshCount;    ///< количество refreshFsInfo/gc
    uint32_t errorCount;            ///< количество ошибок
    uint32_t recoveryCount;          ///< количество восстановлений из бэкапа
    uint32_t lastHealthCheck;        ///< время последней проверки здоровья

    char pathBuffer[MAX_PATH_LEN];   ///< буфер для путей (включая суффикс .tmp)

    /// Потоковая копия src → dest (через dest.tmp + rename), чанки по scratch-буферу.
    bool copyFileAtomic_(const char* srcPath, const char* destPath);

    // Получить метаданные файла из реестра
    const FileMetadata* getMetadata(const char* path);

};

extern FsManager fileSystem;

