// src/ota/OtaHandler.Stream.cpp
#include "ota/OtaHandler.h"

#include "core/Core.h"
#include "common/Logger.h"
#include "common/Constants.h"
#include "common/EspHal.h"
#include "fs/FsManager.h"

#include <Update.h>
#include <cstring>

namespace {

// Хвост OTA = UI shell (.gz) + схемы SoftAP. /config.json и /programs/* не пишем.
bool isAllowedOtaTailPath(const char* path) {
    if (!path || path[0] != '/') return false;
    char logical[BufferBytes::Fs::WEB_ASSET_PATH];
    char gz[BufferBytes::Fs::WEB_ASSET_GZIP_PATH];
    for (size_t i = 0; i < WebAssets::REQUIRED_COUNT; i++) {
        const char* ptr = WebAssets::REQUIRED[i];
        if (!ptr) continue;
        strlcpy(logical, ptr, sizeof(logical));
        if (logical[0] == '\0') continue;
        strlcpy(gz, logical, sizeof(gz));
        strlcat(gz, ".gz", sizeof(gz));
        if (strcmp(path, gz) == 0) return true;
    }
    static const char* const kSchemaPaths[] = {
        "/ui/settings.ui.json",
        "/ui/program-steps.ui.json",
        "/validation/settings.schema.json",
        "/validation/program.schema.json",
        "/validation/programs.index.schema.json",
    };
    for (const char* allowed : kSchemaPaths) {
        if (strcmp(path, allowed) == 0) return true;
    }
    return false;
}

} // namespace

bool OtaHandler::feedOne(const uint8_t* data, size_t len, size_t& consumed) {
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
                const size_t need = Ota::HEADER_SIZE - _hdrHave;
                const size_t take = left < need ? left : need;
                memcpy(_hdrBuf + _hdrHave, p, take);
                _hdrHave = static_cast<uint8_t>(_hdrHave + take);
                consumed += take;
                if (_hdrHave < Ota::HEADER_SIZE) return true;

                _fwSize = static_cast<uint32_t>(_hdrBuf[0]) |
                          (static_cast<uint32_t>(_hdrBuf[1]) << 8) |
                          (static_cast<uint32_t>(_hdrBuf[2]) << 16) |
                          (static_cast<uint32_t>(_hdrBuf[3]) << 24);
                logger.log(LogModule::OtaHandler, "firmware size: %u bytes\n", (unsigned)_fwSize);
                if (_fwSize == 0 || _fwSize > Ota::APP_IMAGE_MAX) {
                    failStream("fwSize вне диапазона");
                    return false;
                }
                yield();
                logger.log(LogModule::OtaHandler, "heap before Update.begin: free=%u maxBlk=%u\n",
                           (unsigned)espHalFreeHeap(), (unsigned)espHalMaxBlock());
                if (!Update.begin(_fwSize)) {
                    core.logHeapSnapshot("ota_update_begin_failed");
                    failStream("Update.begin не удался");
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
                    failStream("Update.write не удался");
                    return false;
                }
                _fwRemaining -= take;
                consumed += take;
                core.feedWatchdog();
                if (_fwRemaining == 0) {
                    // ESP32: LittleFS open/write падает, пока сеанс Update ещё открыт (та же SPI flash).
                    if (!Update.end(true)) {
                        core.logHeapSnapshot("ota_update_end_failed");
                        failStream("Update.end после прошивки не удался");
                        return false;
                    }
                    _updateBegun = false;
                    core.logHeapSnapshot("ota_update_end_ok");
                    // end уже сделал новый слот загрузочным. Сразу вернуть boot на
                    // running: между этим и концом хвоста питание не должно грузить новый UI.
                    if (!pinBootToRunningAfterEnd()) {
                        failStream("не удалось закрепить boot после прошивки");
                        return false;
                    }
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
                    failStream("имя файла слишком длинное");
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
                // LittleFS ждёт абсолютные пути (/…).
                if (_nameBuf[0] != '/') {
                    if (_nameLen + 1 >= sizeof(_nameBuf)) {
                        failStream("имя файла слишком длинное для слэша");
                        return false;
                    }
                    memmove(_nameBuf + 1, _nameBuf, _nameLen + 1);
                    _nameBuf[0] = '/';
                    _nameLen++;
                }
                // Иначе хвост может перезаписать config или программы.
                if (!isAllowedOtaTailPath(_nameBuf)) {
                    logger.log(LogModule::OtaHandler, "UI tail path rejected: %s\n", _nameBuf);
                    failStream("путь хвоста не ассет UI");
                    return false;
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
                logger.log(LogModule::OtaHandler, "extract %s (%u bytes)\n", _nameBuf, (unsigned)_fileSize);

                if (_fileSize == 0) {
                    // Пустой файл: open/close обязательны; сбой не оставлять «Done» со старым хвостом.
                    File f = fileSystem.openWriteStream(_nameBuf, 0);
                    if (!f) {
                        failStream("openWriteStream пустого файла не удался");
                        return false;
                    }
                    if (!fileSystem.closeWriteStream(f, _nameBuf, true)) {
                        failStream("closeWriteStream пустого файла не удался");
                        return false;
                    }
                    _phase = Phase::NameLen;
                    _metaHave = 0;
                    _metaNeed = 2;
                    break;
                }

                _outFile = fileSystem.openWriteStream(_nameBuf, _fileSize);
                if (!_outFile) {
                    failStream("openWriteStream не удался");
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
                    failStream("запись ФС не удалась");
                    return false;
                }
                _fileRemaining -= take;
                consumed += take;
                core.feedWatchdog();
                if (_fileRemaining == 0) {
                    if (!closeOutFile(true)) {
                        failStream("фиксация или переименование ФС не удались");
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

bool OtaHandler::streamFeed(const uint8_t* data, size_t len) {
    if (_phase == Phase::Idle) {
        _phase = Phase::Header;
    }
    if (_phase == Phase::Failed || _phase == Phase::Done) {
        return _phase == Phase::Done;
    }
    if (!data && len > 0) return false;

    size_t offset = 0;
    while (offset < len) {
        if (_totalAccepted + (len - offset) > Ota::FILE_MAX_SIZE) {
            failStream("пакет слишком велик");
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

bool OtaHandler::streamFinish() {
    if (_phase == Phase::Failed) return false;

    // Прошивка должна быть целиком.
    if (_phase == Phase::Header || _phase == Phase::Firmware) {
        failStream("прошивка неполная");
        return false;
    }
    if (_phase == Phase::Name || _phase == Phase::FileSize || _phase == Phase::FileData) {
        failStream("хвост ФС неполный");
        return false;
    }

    // NameLen, и байт больше нет: считать концом пакета (хвостовой nameLen 0 не обязателен).
    if (_phase == Phase::NameLen) {
        if (_metaHave == 0) {
            _phase = Phase::Done;
        } else if (_metaHave == 2 && (_metaBuf[0] | (_metaBuf[1] << 8)) == 0) {
            _phase = Phase::Done;
        } else {
            failStream("метаданные ФС обрезаны");
            return false;
        }
    }

    if (_phase != Phase::Done) {
        failStream("поток не завершён");
        return false;
    }

    if (_updateBegun) {
        if (!Update.end(true)) {
            core.logHeapSnapshot("ota_update_end_failed");
            failStream("Update.end не удался");
            return false;
        }
        _updateBegun = false;
        core.logHeapSnapshot("ota_update_end_ok");
        if (!pinBootToRunningAfterEnd()) {
            failStream("не удалось закрепить boot после прошивки");
            return false;
        }
    }

    // До этой точки boot на running. Новый слот — только когда пакет реально цел.
    if (!activatePendingBoot()) {
        failStream("не удалось активировать новый слот boot");
        return false;
    }

    _streamDoneOk = true;
    logger.log(LogModule::OtaHandler, "stream completed ok, total=%u\n", (unsigned)_totalAccepted);
    return true;
}
