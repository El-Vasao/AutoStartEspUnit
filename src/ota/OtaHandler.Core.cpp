// src/ota/OTAHandler.Core.cpp
#include "ota/OTAHandler.h"

#include "core/Core.h"
#include "common/Logger.h"
#include "common/Constants.h"
#include "fs/FSManager.h"

#include <Update.h>

OTAHandler::OTAHandler() = default;

void OTAHandler::resetStreamState() {
    closeOutFile(false);
    _phase = Phase::Idle;
    _updateBegun = false;
    _streamDoneOk = false;
    _totalAccepted = 0;
    _hdrHave = 0;
    _fwSize = 0;
    _fwRemaining = 0;
    _metaHave = 0;
    _metaNeed = 0;
    _nameLen = 0;
    _nameHave = 0;
    _nameBuf[0] = '\0';
    _fileSize = 0;
    _fileRemaining = 0;
}

bool OTAHandler::closeOutFile(bool commit) {
    if (!_outOpen) return true;
    bool ok = true;
    if (_outFile) {
        ok = fileSystem.closeWriteStream(_outFile, _nameBuf, commit);
    }
    _outOpen = false;
    return ok;
}

void OTAHandler::failStream(const char* why) {
    if (why && why[0]) {
        logger.log("[OTAHandler] stream fail: %s\n", why);
    }
    closeOutFile(false);
    if (_updateBegun) {
        Update.abort();
        _updateBegun = false;
    }
    _phase = Phase::Failed;
    _streamDoneOk = false;
}

void OTAHandler::prepareHttpUploadSession() {
    core.logHeapSnapshot("ota_wait_upload");
    resetStreamState();
    _phase = Phase::Header;
    _awaitHttpUploadSession = true;
    _httpUploadDone = false;
    _httpUploadOk = false;
    _httpSessionStartMs = millis();
}

void OTAHandler::notifyHttpUploadComplete(bool ok) {
    _httpUploadDone = true;
    _httpUploadOk = ok;
    core.logHeapSnapshot(ok ? "ota_upload_complete_ok" : "ota_upload_complete_fail");
}

void OTAHandler::onModeExit() {
    if (_phase != Phase::Done && _phase != Phase::Idle) {
        streamAbort();
    }
    _ongoing = false;
    _awaitHttpUploadSession = false;
    _httpUploadDone = false;
    _httpUploadOk = false;
    _lastLogTime = 0;
    resetStreamState();
}

void OTAHandler::begin() {
    logger.log("[OTAHandler] OTA update started (stream)\n");
    core.logHeapSnapshot("ota_mode_begin");
    _ongoing = true;
    _lastLogTime = 0;
}

void OTAHandler::streamAbort() {
    failStream("abort");
}

void OTAHandler::update() {
    if (!_ongoing) return;

    const uint32_t now = millis();

    if (_awaitHttpUploadSession && !_httpUploadDone) {
        if ((uint32_t)(now - _httpSessionStartMs) >= Timing::OTA_HTTP_UPLOAD_IDLE_MS) {
            logger.log("[OTAHandler] HTTP upload idle timeout (no final within %u ms), leaving OTA\n",
                       (unsigned)Timing::OTA_HTTP_UPLOAD_IDLE_MS);
            streamAbort();
            core.onOtaHttpUploadAwaitTimedOut();
            return;
        }
        if (now - _lastLogTime > Timing::OTA_WAIT_LOG_INTERVAL_MS) {
            logger.log("[OTAHandler] Receiving firmware stream… (%u bytes so far)\n",
                       (unsigned)_totalAccepted);
            _lastLogTime = now;
        }
        return;
    }

    if (_awaitHttpUploadSession && _httpUploadDone && !_httpUploadOk) {
        logger.log("[OTAHandler] HTTP upload/stream failed, leaving OTA mode\n");
        core.logHeapSnapshot("ota_mode_upload_failed");
        streamAbort();
        core.exitOtaToNormalMode();
        return;
    }

    if (_awaitHttpUploadSession && _httpUploadDone && _httpUploadOk) {
        if (_streamDoneOk || _phase == Phase::Done) {
            logger.log("[OTAHandler] Stream OTA successful, rebooting…\n");
            core.logHeapSnapshot("ota_update_success");
            delay(Delays::REBOOT_HTTP_REPLY_MS);
            core.reboot();
        } else {
            logger.log("[OTAHandler] Upload OK but stream not Done, rebooting…\n");
            core.logHeapSnapshot("ota_update_failed");
            streamAbort();
            delay(Delays::REBOOT_HTTP_REPLY_MS);
            core.reboot();
        }
        _ongoing = false;
    }
}
