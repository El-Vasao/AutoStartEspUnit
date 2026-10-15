// include/ota/OTAHandler.h
#pragma once

#include <Arduino.h>
#include <FS.h>

/**
 * @brief OTA: HTTP stream → Update (firmware) + LittleFS file tail.
 *
 * Package format (unchanged):
 * 1) fwSize (uint32 LE)
 * 2) firmware bytes (fwSize)
 * 3) optional FS records: nameLen(u16 LE), name, fileSize(u32 LE), data; nameLen==0 ends.
 *
 * No full `/update.bin` on disk — chunks from POST /upload feed the FSM directly.
 */
class OTAHandler {
public:
    OTAHandler();

    void begin();
    void update();
    void onModeExit();

    /// Start waiting for multipart upload; resets stream FSM.
    void prepareHttpUploadSession();

    /// Feed package bytes from HTTP upload callback.
    bool streamFeed(const uint8_t* data, size_t len);

    /// Multipart final: finish Update + open FS file; returns false on error.
    bool streamFinish();

    /// Abort Update / open FS write; leave stream Failed.
    void streamAbort();

    /// HTTP upload finished (ok/fail) — drives mode exit / reboot in update().
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
    bool closeOutFile(bool commit);
    bool feedOne(const uint8_t* data, size_t len, size_t& consumed);
};
