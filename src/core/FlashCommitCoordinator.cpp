#include "core/FlashCommitCoordinator.h"

#include "core/Core.h"
#include "program/ProgramExecutor.h"
#include "config/Config.h"
#include "fs/FsManager.h"
#include "web/WebServer.h"
#include "common/Constants.h"
#include "common/ErrorCodes.h"
#include "common/EspHal.h"
#include "common/Logger.h"
#include "core/ErrorManager.h"

#include <cstring>

FlashCommitCoordinator flashCommit;

namespace {

// Тело POST живёт здесь, пока tick() не запишет tmp. Колбэк async в ФС не заходит.
uint8_t gPostedBody[Limits::CONFIG_JSON_SIZE];
size_t gPostedLen = 0;
size_t gPostedExpect = 0;
bool gPostedArmed = false;

bool flushPostedBody(const char* path) {
    const size_t n = gPostedLen;
    const bool okLen = gPostedArmed && n == gPostedExpect && n > 0 && n <= Limits::CONFIG_JSON_SIZE;
    gPostedArmed = false;
    gPostedLen = 0;
    gPostedExpect = 0;
    if (!okLen) return false;
    // tmp пишет цикл; apply ниже переименовывает его в боевой файл.
    if (!fileSystem.ensurePath(path)) return false;
    File f = fileSystem.openDirectWrite(path, n);
    if (!f) return false;
    const size_t w = f.write(gPostedBody, n);
    f.close();
    if (w != n) {
        fileSystem.deleteFile(path);
        return false;
    }
    return true;
}

} // namespace

bool FlashCommitCoordinator::armPostedBody(bool isConfig, size_t total) {
    (void)isConfig;
    if (gPostedArmed || deferredPostedConfigApply_ || deferredPostedProgramApply_) return false;
    if (total == 0 || total > Limits::CONFIG_JSON_SIZE) return false;
    gPostedLen = 0;
    gPostedExpect = total;
    gPostedArmed = true;
    return true;
}

bool FlashCommitCoordinator::appendPostedBody(size_t index, const uint8_t* data, size_t len) {
    if (!gPostedArmed || !data || len == 0) return false;
    if (index != gPostedLen || len > gPostedExpect - gPostedLen) return false;
    memcpy(gPostedBody + gPostedLen, data, len);
    gPostedLen += len;
    return true;
}

bool FlashCommitCoordinator::postedBodyComplete() const {
    return gPostedArmed && gPostedExpect > 0 && gPostedLen == gPostedExpect;
}

void FlashCommitCoordinator::dropPostedBody() {
    gPostedArmed = false;
    gPostedLen = 0;
    gPostedExpect = 0;
}

void FlashCommitCoordinator::recordFlashCommitSnapshot(FlashCommitOp op, bool ok, bool changed, uint16_t crc) {
    lastFlashOp_ = op;
    lastFlashOk_ = ok;
    lastFlashMillis_ = millis();
    lastFlashChanged_ = changed;
    lastFlashCrc_ = crc;
}

bool FlashCommitCoordinator::queueResetConfig() {
    if ((deferredPostedConfigApply_ || deferredPostedProgramApply_) ||
        (pendingFsOp_ != PendingFsOp::NONE && pendingFsOp_ != PendingFsOp::RESET_CONFIG)) {
        logger.log(LogModule::FlashCommitCoordinator, "queue rejected reset_config (busy %u)\n",
                   (unsigned)pendingFsOp_);
        recordFlashCommitSnapshot(FlashCommitOp::RESET_CONFIG, false, false, lastFlashCrc_);
        webServer.broadcastStatusForce();
        return false;
    }
    pendingFsOp_ = PendingFsOp::RESET_CONFIG;
    return true;
}

bool FlashCommitCoordinator::queueResetPrograms() {
    if ((deferredPostedConfigApply_ || deferredPostedProgramApply_) ||
        (pendingFsOp_ != PendingFsOp::NONE && pendingFsOp_ != PendingFsOp::RESET_PROGRAMS)) {
        logger.log(LogModule::FlashCommitCoordinator, "queue rejected reset_programs (busy %u)\n",
                   (unsigned)pendingFsOp_);
        recordFlashCommitSnapshot(FlashCommitOp::RESET_PROGRAMS, false, false, lastFlashCrc_);
        webServer.broadcastStatusForce();
        return false;
    }
    pendingFsOp_ = PendingFsOp::RESET_PROGRAMS;
    return true;
}

bool FlashCommitCoordinator::queueDeleteProgram(uint8_t id) {
    if ((deferredPostedConfigApply_ || deferredPostedProgramApply_) ||
        (pendingFsOp_ != PendingFsOp::NONE &&
         !(pendingFsOp_ == PendingFsOp::DELETE_PROGRAM && pendingProgramId_ == id))) {
        logger.log(LogModule::FlashCommitCoordinator, "queue rejected delete_program %u (busy %u)\n",
                   (unsigned)id, (unsigned)pendingFsOp_);
        recordFlashCommitSnapshot(FlashCommitOp::DELETE_PROGRAM, false, false, lastFlashCrc_);
        webServer.broadcastStatusForce();
        return false;
    }
    pendingProgramId_ = id;
    pendingFsOp_ = PendingFsOp::DELETE_PROGRAM;
    return true;
}

void FlashCommitCoordinator::tick(Core& core) {
    core.feedWatchdog();
    espHalFeedWdt();

    // Пауза на время программы принадлежит Core::update (tick не вызывается, пока исполнитель работает).

    if (deferredPostedConfigApply_) {
        core.feedWatchdog();
        espHalFeedWdt();
        logger.log(LogModule::FlashCommitCoordinator, "config: starting deferred apply %s (heap=%u)\n",
                   HttpPostJson::TMP_CONFIG, espHalFreeHeap());
        if (!flushPostedBody(HttpPostJson::TMP_CONFIG)) {
            recordFlashCommitSnapshot(FlashCommitOp::SAVE_CONFIG, false, false, config.getCRC());
            logger.log(LogModule::FlashCommitCoordinator, "config: failed to write tmp from RAM\n");
            webServer.broadcastStatusForce();
            deferredPostedConfigApply_ = false;
            fileSystem.refreshFsInfo();
            return;
        }
        const uint16_t beforeCrc = config.getCRC();
        const bool cfgOk =
            config.applyPostedConfigJsonFile(HttpPostJson::TMP_CONFIG, core.getSensors());
        core.feedWatchdog();
        espHalFeedWdt();

        if (!cfgOk) {
            recordFlashCommitSnapshot(FlashCommitOp::SAVE_CONFIG, false, false, config.getCRC());
            logger.log(LogModule::FlashCommitCoordinator, "config: apply failed\n");
            webServer.broadcastStatusForce();
        } else {
            const uint16_t afterCrc = config.getCRC();
            const bool changed = (beforeCrc != afterCrc);
            recordFlashCommitSnapshot(FlashCommitOp::SAVE_CONFIG, true, changed, afterCrc);
            core.setRebootRequired(true);
            logger.log(LogModule::FlashCommitCoordinator, "config: /config.json written (changed=%d crc:%u->%u heap=%u)\n",
                       (int)changed,
                       (unsigned)beforeCrc,
                       (unsigned)afterCrc,
                       espHalFreeHeap());
            webServer.broadcastStatusForce();
        }
        deferredPostedConfigApply_ = false;
        fileSystem.refreshFsInfo();
        return;
    }

    if (deferredPostedProgramApply_) {
        core.feedWatchdog();
        espHalFeedWdt();

        if (!deferredPostedProgramIndexPhase_) {
            logger.log(LogModule::FlashCommitCoordinator, "program: starting deferred apply %s (heap=%u)\n",
                       HttpPostJson::TMP_PROGRAM, espHalFreeHeap());
            if (!flushPostedBody(HttpPostJson::TMP_PROGRAM)) {
                recordFlashCommitSnapshot(FlashCommitOp::SAVE_PROGRAM, false, false, config.getCRC());
                logger.log(LogModule::FlashCommitCoordinator, "program: failed to write tmp from RAM\n");
                webServer.broadcastStatusForce();
                deferredPostedProgramApply_ = false;
                fileSystem.refreshFsInfo();
                return;
            }
            uint8_t postedId = 0;
            ErrorCode commitErr = ErrorCode::NONE;
            const bool parsedOk = config.commitPostedProgramFile(HttpPostJson::TMP_PROGRAM, &postedId, &commitErr);
            if (!parsedOk) {
                if (commitErr != ErrorCode::NONE) core.getErrorManager().set(commitErr);
                logger.log(LogModule::FlashCommitCoordinator, "program: tmp apply failed (%s)\n",
                           errorCodeToString(commitErr));
                recordFlashCommitSnapshot(FlashCommitOp::SAVE_PROGRAM, false, false, config.getCRC());
                webServer.broadcastStatusForce();
                deferredPostedProgramApply_ = false;
                fileSystem.refreshFsInfo();
            } else {
                logger.log(LogModule::FlashCommitCoordinator, "program: JSON ok id=%u, file written, rebuildProgramIndex scheduled\n",
                           (unsigned)postedId);
                deferredPostedProgramIndexPhase_ = true;
                webServer.broadcastStatusForce();
                core.feedWatchdog();
                espHalFeedWdt();
                return; // следующий тик пересоберёт индекс
            }
        } else {
            logger.log(LogModule::FlashCommitCoordinator, "program: deferred rebuildProgramIndex (heap=%u)\n", espHalFreeHeap());
            const bool ok = config.rebuildProgramIndex();
            recordFlashCommitSnapshot(FlashCommitOp::SAVE_PROGRAM, ok, ok, config.getCRC());
            logger.log(LogModule::FlashCommitCoordinator, "program: flash write %s (heap=%u)\n", ok ? "ok" : "failed", espHalFreeHeap());
            webServer.broadcastStatusForce();
            deferredPostedProgramApply_ = false;
            deferredPostedProgramIndexPhase_ = false;
            fileSystem.refreshFsInfo();
        }
        return;
    }

    if (pendingFsOp_ == PendingFsOp::NONE) return;

    const PendingFsOp op = pendingFsOp_;
    pendingFsOp_ = PendingFsOp::NONE;

    logger.log(LogModule::FlashCommitCoordinator, "deferred FS op: %u start (heap=%u)\n", (unsigned)op, espHalFreeHeap());
    core.feedWatchdog();
    espHalFeedWdt();
    bool ok = false;
    FlashCommitOp flashOp = FlashCommitOp::NONE;
    if (op == PendingFsOp::DELETE_PROGRAM) {
        logger.log(LogModule::FlashCommitCoordinator, "delete_program deferred: id=%u\n", (unsigned)pendingProgramId_);
        ok = config.deleteProgram(pendingProgramId_);
        flashOp = FlashCommitOp::DELETE_PROGRAM;
    } else if (op == PendingFsOp::RESET_CONFIG) {
        logger.log(LogModule::FlashCommitCoordinator, "reset_config deferred\n");
        ok = config.reset();
        flashOp = FlashCommitOp::RESET_CONFIG;
        if (ok) core.setRebootRequired(true);
    } else if (op == PendingFsOp::RESET_PROGRAMS) {
        logger.log(LogModule::FlashCommitCoordinator, "reset_programs deferred\n");
        ok = config.resetPrograms();
        flashOp = FlashCommitOp::RESET_PROGRAMS;
    }
    recordFlashCommitSnapshot(flashOp, ok, ok, config.getCRC());
    logger.log(LogModule::FlashCommitCoordinator, "deferred FS op: %u done ok=%d crc=%u heap=%u\n",
               (unsigned)op, (int)ok, (unsigned)lastFlashCrc_, espHalFreeHeap());
    fileSystem.refreshFsInfo();
}

