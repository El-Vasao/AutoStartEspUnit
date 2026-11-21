// src/ota/OtaHandler.Core.cpp
#include "ota/OtaHandler.h"

#include "core/Core.h"
#include "common/Logger.h"
#include "common/Constants.h"
#include "fs/FsManager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <Update.h>
#include <esp_ota_ops.h>

namespace {
// Кольцо async → цикл: _httpRingHead и _httpRingCount меняются только под этой секцией.
// Байты в слоты пишет и читает вне секции: async пишет только свободные, цикл — только занятые.
portMUX_TYPE s_httpRingMux = portMUX_INITIALIZER_UNLOCKED;
} // пространство имён

OtaHandler::OtaHandler() = default;

void OtaHandler::resetStreamState() {
    closeOutFile(false);
    _phase = Phase::Idle;
    _updateBegun = false;
    _fwCommitted = false;
    _pendingBoot = nullptr;
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
    _streamFinishDone = false;
}

bool OtaHandler::pinBootToRunningAfterEnd() {
    // end записал в otadata слот, выбранный в begin через
    // esp_ota_get_next_update_partition(NULL). Читаем boot сразу: это он.
    _pendingBoot = esp_ota_get_boot_partition();
    const esp_partition_t* running = esp_ota_get_running_partition();
    _fwCommitted = true;
    if (!_pendingBoot || !running || _pendingBoot == running) {
        logger.log(LogModule::OtaHandler, "no separate slot after Update.end\n");
        _pendingBoot = nullptr;
        return false;
    }
    if (esp_ota_set_boot_partition(running) != ESP_OK) {
        logger.log(LogModule::OtaHandler, "failed to pin boot to current image\n");
        return false;
    }
    logger.log(LogModule::OtaHandler, "boot pinned to current image (%s), expected %s\n",
               running->label, _pendingBoot->label);
    return true;
}

bool OtaHandler::activatePendingBoot() {
    if (!_pendingBoot || esp_ota_set_boot_partition(_pendingBoot) != ESP_OK) {
        logger.log(LogModule::OtaHandler, "failed to activate new boot slot\n");
        return false;
    }
    logger.log(LogModule::OtaHandler, "boot switched to updated slot (%s)\n", _pendingBoot->label);
    return true;
}

bool OtaHandler::closeOutFile(bool commit) {
    if (!_outOpen) return true;
    bool ok = true;
    if (_outFile) {
        ok = fileSystem.closeWriteStream(_outFile, _nameBuf, commit);
    }
    _outOpen = false;
    return ok;
}

void OtaHandler::failStream(const char* why) {
    if (why && why[0]) {
        logger.log(LogModule::OtaHandler, "stream failed: %s\n", why);
    }
    closeOutFile(false);
    // Сессия ещё открыта только до Update.end. После end abort слот не возвращает.
    if (_updateBegun) {
        Update.abort();
        _updateBegun = false;
    }
    // Boot уже должен быть на running (pin сразу после end). Закрепить ещё раз:
    // rollBack() здесь нельзя — next_update(NULL) на этом IDF считается от running,
    // то есть снова указывает на новый слот, а не на выполняемый образ.
    if (_fwCommitted) {
        const esp_partition_t* running = esp_ota_get_running_partition();
        const bool restored = running && esp_ota_set_boot_partition(running) == ESP_OK;
        if (restored) {
            logger.log(LogModule::OtaHandler, "boot reverted to runnable image\n");
            _fwCommitted = false;
            _pendingBoot = nullptr;
        } else {
            logger.log(LogModule::OtaHandler, "failed to revert boot\n");
        }
    }
    _phase = Phase::Failed;
    _streamDoneOk = false;
}

void OtaHandler::prepareHttpUploadSession() {
    core.logHeapSnapshot("ota_wait_upload");
    // Вызов из async (index==0): LittleFS и Update не трогаем. Парсер сбросит begin() на цикле.
    resetHttpRing_();
    _httpFinalMarked = false;
    _phase = Phase::Header;
    _awaitHttpUploadSession = true;
    _httpUploadDone = false;
    _httpUploadOk = false;
    _httpSessionStartMs = millis();
}

bool OtaHandler::holdsFlash() const {
    return _phase != Phase::Idle && _phase != Phase::Done && _phase != Phase::Failed;
}

bool OtaHandler::enqueueHttpBytes(const uint8_t* data, size_t len) {
    if (_phase == Phase::Failed) return false;
    if (!data && len > 0) return false;
    size_t off = 0;
    bool stalled = false;
    uint32_t stallStartMs = 0;
    while (off < len) {
        // Индексы читаем и меняем только под секцией. Байты пишем в свободные слоты уже вне её.
        taskENTER_CRITICAL(&s_httpRingMux);
        const uint16_t free = httpRingFree_();
        const uint16_t tail =
            static_cast<uint16_t>((_httpRingHead + _httpRingCount) % Ota::HTTP_RING_SIZE);
        taskEXIT_CRITICAL(&s_httpRingMux);
        if (free == 0) {
            const uint32_t now = millis();
            if (!stalled) {
                stalled = true;
                stallStartMs = now;
            }
            // Сессия уже сорвана (abort или ошибка цикла): не ждём.
            if (_phase == Phase::Failed) return false;
            if ((uint32_t)(now - stallStartMs) > Timing::OTA_RING_WAIT_MS) {
                // Цикл не слил кольцо за лимит: поток Failed, колбэк ответит ошибкой и вызовет abort.
                logger.log(LogModule::OtaHandler,
                           "HTTP ring stalled (no drain for %u ms), upload aborted\n",
                           (unsigned)Timing::OTA_RING_WAIT_MS);
                _phase = Phase::Failed;
                return false;
            }
            // Отдать квант loopTask, чтобы drainHttpRing_ забрал байты.
            delay(1);
            continue;
        }
        stalled = false;
        const size_t n = (len - off) < free ? (len - off) : free;
        for (size_t i = 0; i < n; i++) {
            _httpRing[(tail + i) % Ota::HTTP_RING_SIZE] = data[off + i];
        }
        taskENTER_CRITICAL(&s_httpRingMux);
        _httpRingCount = static_cast<uint16_t>(_httpRingCount + n);
        taskEXIT_CRITICAL(&s_httpRingMux);
        off += n;
    }
    return true;
}

bool OtaHandler::markHttpFinal() {
    _httpFinalMarked = true;
    return true;
}

void OtaHandler::drainHttpRing_() {
    uint8_t chunk[64];
    for (;;) {
        // Сливает только цикл. Байты читаем вне секции, head и count двигаем под ней.
        taskENTER_CRITICAL(&s_httpRingMux);
        const uint16_t head = _httpRingHead;
        const uint16_t n =
            _httpRingCount < sizeof(chunk) ? _httpRingCount : static_cast<uint16_t>(sizeof(chunk));
        taskEXIT_CRITICAL(&s_httpRingMux);
        if (n == 0) return;
        for (uint16_t i = 0; i < n; i++) {
            chunk[i] = _httpRing[(head + i) % Ota::HTTP_RING_SIZE];
        }
        taskENTER_CRITICAL(&s_httpRingMux);
        _httpRingHead = static_cast<uint16_t>((head + n) % Ota::HTTP_RING_SIZE);
        _httpRingCount = static_cast<uint16_t>(_httpRingCount - n);
        taskEXIT_CRITICAL(&s_httpRingMux);
        if (!streamFeed(chunk, n)) {
            // Кольцо не сбрасываем молча: поток Failed, async выйдет по enqueue.
            _phase = Phase::Failed;
            return;
        }
    }
}

void OtaHandler::resetHttpRing_() {
    taskENTER_CRITICAL(&s_httpRingMux);
    _httpRingHead = 0;
    _httpRingCount = 0;
    taskEXIT_CRITICAL(&s_httpRingMux);
}

void OtaHandler::notifyHttpUploadComplete(bool ok) {
    _httpUploadDone = true;
    _httpUploadOk = ok;
    core.logHeapSnapshot(ok ? "ota_upload_complete_ok" : "ota_upload_complete_fail");
}

void OtaHandler::onModeExit() {
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

void OtaHandler::begin() {
    logger.log(LogModule::OtaHandler, "OTA update started (stream)\n");
    core.logHeapSnapshot("ota_mode_begin");
    // Парсер и открытый файл сбрасываем здесь, на цикле. Кольцо и флаги HTTP не трогаем: байты и финал могли прийти раньше.
    resetStreamState();
    _phase = Phase::Header;
    _ongoing = true;
    _lastLogTime = 0;
}

void OtaHandler::streamAbort() {
    failStream("отмена");
}

void OtaHandler::update() {
    if (!_ongoing) return;

    const uint32_t now = millis();

    drainHttpRing_();
    if (_httpFinalMarked && !_streamFinishDone && _httpRingCount == 0) {
        _streamFinishDone = true;
        if (!streamFinish()) {
            logger.log(LogModule::OtaHandler, "streamFinish failed after HTTP final\n");
        }
    }

    if (_awaitHttpUploadSession && !_httpUploadDone) {
        if ((uint32_t)(now - _httpSessionStartMs) >= Timing::OTA_HTTP_UPLOAD_IDLE_MS) {
            logger.log(LogModule::OtaHandler, "HTTP download idle timeout (no final in %u ms), exit OTA\n",
                       (unsigned)Timing::OTA_HTTP_UPLOAD_IDLE_MS);
            streamAbort();
            core.onOtaHttpUploadAwaitTimedOut();
            return;
        }
        if (now - _lastLogTime > Timing::OTA_WAIT_LOG_INTERVAL_MS) {
            logger.log(LogModule::OtaHandler, "receiving firmware stream… (%u bytes so far)\n",
                       (unsigned)_totalAccepted);
            _lastLogTime = now;
        }
        return;
    }

    if (_awaitHttpUploadSession && _httpUploadDone && !_httpUploadOk) {
        logger.log(LogModule::OtaHandler, "HTTP download/stream failed, exit OTA mode\n");
        core.logHeapSnapshot("ota_mode_upload_failed");
        streamAbort();
        core.exitOtaToNormalMode();
        return;
    }

    if (_awaitHttpUploadSession && _httpUploadDone && _httpUploadOk) {
        if (!_streamFinishDone) return;
        if (_streamDoneOk || _phase == Phase::Done) {
            logger.log(LogModule::OtaHandler, "OTA stream ok, rebooting…\n");
            core.logHeapSnapshot("ota_update_success");
            delay(Delays::REBOOT_HTTP_REPLY_MS);
            core.reboot();
        } else {
            logger.log(LogModule::OtaHandler, "download ok, but stream not Done, exit OTA mode\n");
            core.logHeapSnapshot("ota_update_failed");
            streamAbort();
            core.exitOtaToNormalMode();
        }
        _ongoing = false;
    }
}
