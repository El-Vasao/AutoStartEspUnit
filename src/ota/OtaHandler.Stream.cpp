// src/ota/OTAHandler.Stream.cpp
#include "ota/OTAHandler.h"

#include "core/Core.h"
#include "common/Logger.h"
#include "common/Constants.h"
#include "common/EspHal.h"
#include "fs/FSManager.h"

#include <Update.h>
#include <cstring>

bool OTAHandler::feedOne(const uint8_t* data, size_t len, size_t& consumed) {
    consumed = 0;
    if (!data || len == 0) return true;
    if (_phase == Phase::Failed || _phase == Phase::Done || _phase == Phase::Idle) {
        return _phase == Phase::Done;
    }

    while (consumed < len) {
        const uint8_t* p = data + consumed;
        const size_t left = len - consumed;

        switch (_phase) {
            case Phase::Header: {
                const size_t need = OTA::HEADER_SIZE - _hdrHave;
                const size_t take = left < need ? left : need;
                memcpy(_hdrBuf + _hdrHave, p, take);
                _hdrHave = static_cast<uint8_t>(_hdrHave + take);
                consumed += take;
                if (_hdrHave < OTA::HEADER_SIZE) return true;

                _fwSize = static_cast<uint32_t>(_hdrBuf[0]) |
                          (static_cast<uint32_t>(_hdrBuf[1]) << 8) |
                          (static_cast<uint32_t>(_hdrBuf[2]) << 16) |
                          (static_cast<uint32_t>(_hdrBuf[3]) << 24);
                logger.log("[OTAHandler] Firmware size: %u bytes\n", (unsigned)_fwSize);
                if (_fwSize == 0 || _fwSize > OTA::APP_IMAGE_MAX) {
                    failStream("fwSize out of range");
                    return false;
                }
                yield();
                logger.log("[OTAHandler] heap before Update.begin: free=%u maxBlk=%u\n",
                           (unsigned)espHalFreeHeap(), (unsigned)espHalMaxBlock());
                if (!Update.begin(_fwSize)) {
                    core.logHeapSnapshot("ota_update_begin_failed");
                    failStream("Update.begin failed");
                    return false;
                }
                _updateBegun = true;
                core.logHeapSnapshot("ota_update_begin_ok");
                _fwRemaining = _fwSize;
                _phase = Phase::Firmware;
                break;
            }

            case Phase::Firmware: {
                const size_t take = left < _fwRemaining ? left : _fwRemaining;
                if (Update.write(const_cast<uint8_t*>(p), take) != take) {
                    core.logHeapSnapshot("ota_update_write_failed");
                    failStream("Update.write failed");
                    return false;
                }
                _fwRemaining -= take;
                consumed += take;
                core.feedWatchdog();
                if (_fwRemaining == 0) {
                    // ESP32: LittleFS open/write fails while Update session is still open (same SPI flash).
                    if (!Update.end(true)) {
                        core.logHeapSnapshot("ota_update_end_failed");
                        failStream("Update.end after firmware failed");
                        return false;
                    }
                    _updateBegun = false;
                    core.logHeapSnapshot("ota_update_end_ok");
                    _phase = Phase::NameLen;
                    _metaHave = 0;
                    _metaNeed = 2;
                }
                break;
            }

            case Phase::NameLen: {
                const size_t need = _metaNeed - _metaHave;
                const size_t take = left < need ? left : need;
                memcpy(_metaBuf + _metaHave, p, take);
                _metaHave = static_cast<uint8_t>(_metaHave + take);
                consumed += take;
                if (_metaHave < _metaNeed) return true;

                _nameLen = static_cast<uint16_t>(_metaBuf[0] | (_metaBuf[1] << 8));
                if (_nameLen == 0) {
                    _phase = Phase::Done;
                    return true;
                }
                if (_nameLen >= sizeof(_nameBuf)) {
                    failStream("filename too long");
                    return false;
                }
                _nameHave = 0;
                _phase = Phase::Name;
                break;
            }

            case Phase::Name: {
                const size_t need = _nameLen - _nameHave;
                const size_t take = left < need ? left : need;
                memcpy(_nameBuf + _nameHave, p, take);
                _nameHave = static_cast<uint16_t>(_nameHave + take);
                consumed += take;
                if (_nameHave < _nameLen) return true;
                _nameBuf[_nameLen] = '\0';
                // LittleFS expects absolute paths (/…).
                if (_nameBuf[0] != '/') {
                    if (_nameLen + 1 >= sizeof(_nameBuf)) {
                        failStream("filename too long for slash");
                        return false;
                    }
                    memmove(_nameBuf + 1, _nameBuf, _nameLen + 1);
                    _nameBuf[0] = '/';
                    _nameLen++;
                }
                _metaHave = 0;
                _metaNeed = 4;
                _phase = Phase::FileSize;
                break;
            }

            case Phase::FileSize: {
                const size_t need = _metaNeed - _metaHave;
                const size_t take = left < need ? left : need;
                memcpy(_metaBuf + _metaHave, p, take);
                _metaHave = static_cast<uint8_t>(_metaHave + take);
                consumed += take;
                if (_metaHave < _metaNeed) return true;

                _fileSize = static_cast<uint32_t>(_metaBuf[0]) |
                            (static_cast<uint32_t>(_metaBuf[1]) << 8) |
                            (static_cast<uint32_t>(_metaBuf[2]) << 16) |
                            (static_cast<uint32_t>(_metaBuf[3]) << 24);
                logger.log("[OTAHandler] Extracting %s (%u bytes)\n", _nameBuf, (unsigned)_fileSize);

                if (_fileSize == 0) {
                    // Empty file: still create via open/close.
                    File f = fileSystem.openWriteStream(_nameBuf, 0);
                    if (f) {
                        fileSystem.closeWriteStream(f, _nameBuf, true);
                    }
                    _phase = Phase::NameLen;
                    _metaHave = 0;
                    _metaNeed = 2;
                    break;
                }

                _outFile = fileSystem.openWriteStream(_nameBuf, _fileSize);
                if (!_outFile) {
                    failStream("openWriteStream failed");
                    return false;
                }
                _outOpen = true;
                _fileRemaining = _fileSize;
                _phase = Phase::FileData;
                break;
            }

            case Phase::FileData: {
                const size_t take = left < _fileRemaining ? left : _fileRemaining;
                if (_outFile.write(p, take) != take) {
                    failStream("FS write failed");
                    return false;
                }
                _fileRemaining -= take;
                consumed += take;
                core.feedWatchdog();
                if (_fileRemaining == 0) {
                    if (!closeOutFile(true)) {
                        failStream("FS finalize/rename failed");
                        return false;
                    }
                    _phase = Phase::NameLen;
                    _metaHave = 0;
                    _metaNeed = 2;
                }
                break;
            }

            default:
                return _phase == Phase::Done;
        }
    }
    return true;
}

bool OTAHandler::streamFeed(const uint8_t* data, size_t len) {
    if (_phase == Phase::Idle) {
        _phase = Phase::Header;
    }
    if (_phase == Phase::Failed || _phase == Phase::Done) {
        return _phase == Phase::Done;
    }
    if (!data && len > 0) return false;

    size_t offset = 0;
    while (offset < len) {
        if (_totalAccepted + (len - offset) > OTA::FILE_MAX_SIZE) {
            failStream("package too large");
            return false;
        }
        size_t consumed = 0;
        if (!feedOne(data + offset, len - offset, consumed)) {
            return false;
        }
        if (consumed == 0) break;
        offset += consumed;
        _totalAccepted += consumed;
        if (_phase == Phase::Done || _phase == Phase::Failed) break;
    }
    return _phase != Phase::Failed;
}

bool OTAHandler::streamFinish() {
    if (_phase == Phase::Failed) return false;

    // Firmware must be complete.
    if (_phase == Phase::Header || _phase == Phase::Firmware) {
        failStream("incomplete firmware");
        return false;
    }
    if (_phase == Phase::Name || _phase == Phase::FileSize || _phase == Phase::FileData) {
        failStream("incomplete FS tail");
        return false;
    }

    // NameLen with no more bytes: treat as end of package (no trailing 0 nameLen required).
    if (_phase == Phase::NameLen) {
        if (_metaHave == 0) {
            _phase = Phase::Done;
        } else if (_metaHave == 2 && (_metaBuf[0] | (_metaBuf[1] << 8)) == 0) {
            _phase = Phase::Done;
        } else {
            failStream("truncated FS meta");
            return false;
        }
    }

    if (_phase != Phase::Done) {
        failStream("stream not complete");
        return false;
    }

    if (_updateBegun) {
        if (!Update.end(true)) {
            core.logHeapSnapshot("ota_update_end_failed");
            failStream("Update.end failed");
            return false;
        }
        _updateBegun = false;
        core.logHeapSnapshot("ota_update_end_ok");
    }

    _streamDoneOk = true;
    logger.log("[OTAHandler] Stream finished OK, total=%u\n", (unsigned)_totalAccepted);
    return true;
}
