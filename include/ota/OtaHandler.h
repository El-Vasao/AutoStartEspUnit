// include/ota/OtaHandler.h
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <esp_partition.h>

/**
 * @brief OTA: поток HTTP → Update (прошивка) + хвост файлов LittleFS.
 *
 * Формат пакета (без изменений):
 * 1) fwSize (uint32 LE)
 * 2) байты прошивки (fwSize)
 * 3) необязательные записи FS: nameLen(u16 LE), name, fileSize(u32 LE), data; nameLen==0 завершает.
 *
 * Целого `/update.bin` на диске нет — куски из POST /upload кормят FSM напрямую.
 */
class OtaHandler {
public:
    OtaHandler();

    void begin();
    void update();
    void onModeExit();

    /// Начать ожидание multipart-загрузки; сбрасывает FSM потока.
    void prepareHttpUploadSession();

    /// Подать байты пакета из callback загрузки HTTP.
    bool streamFeed(const uint8_t* data, size_t len);

    /// Конец multipart: завершить Update и открытый файл FS; false при ошибке.
    bool streamFinish();

    /// Прервать Update / открытую запись FS; поток остаётся в Failed.
    void streamAbort();

    /// Загрузка HTTP закончилась (ok/fail) — update() выходит из режима или перезагружает.
    void notifyHttpUploadComplete(bool ok);

    bool isOngoing() const { return _ongoing; }
    bool streamSucceeded() const { return _streamDoneOk; }

private:
    enum class Phase : uint8_t {
        Idle = 0,
        Header,
        Firmware,
        NameLen,
        Name,
        FileSize,
        FileData,
        Done,
        Failed
    };

    bool _ongoing{false};
    uint32_t _lastLogTime{0};

    bool _awaitHttpUploadSession{false};
    bool _httpUploadDone{false};
    bool _httpUploadOk{false};
    uint32_t _httpSessionStartMs{0};

    Phase _phase{Phase::Idle};
    bool _updateBegun{false};
    // Прошивка записана (Update.end). Boot при этом держим на running, пока
    // streamFinish не подтвердит хвост: иначе обрыв питания загрузит новый слот
    // с нецелым UI. abort после end слот не возвращает.
    bool _fwCommitted{false};
    const esp_partition_t* _pendingBoot{nullptr};
    bool _streamDoneOk{false};
    size_t _totalAccepted{0};

    uint8_t _hdrBuf[4]{};
    uint8_t _hdrHave{0};
    uint32_t _fwSize{0};
    uint32_t _fwRemaining{0};

    uint8_t _metaBuf[4]{};
    uint8_t _metaHave{0};
    uint8_t _metaNeed{0};

    uint16_t _nameLen{0};
    char _nameBuf[64]{};
    uint16_t _nameHave{0};

    uint32_t _fileSize{0};
    uint32_t _fileRemaining{0};
    File _outFile;
    bool _outOpen{false};

    void resetStreamState();
    void failStream(const char* why);
    // end уже выставил boot на новый слот — запомнить его и вернуть running.
    bool pinBootToRunningAfterEnd();
    // Хвост цел: только теперь новый слот можно сделать загрузочным.
    bool activatePendingBoot();
    bool closeOutFile(bool commit);
    bool feedOne(const uint8_t* data, size_t len, size_t& consumed);
};
