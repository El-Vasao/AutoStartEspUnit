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

FlashCommitCoordinator flashCommit;

const char* FlashCommitCoordinator::getCurrentFlashOpString() const {
    if (deferredPostedConfigApply_) return "save_config";
    if (deferredPostedProgramApply_) return "save_program";
    switch (pendingFsOp_) {
        case PendingFsOp::DELETE_PROGRAM: return "delete_program";
        case PendingFsOp::RESET_CONFIG: return "reset_config";
        case PendingFsOp::RESET_PROGRAMS: return "reset_programs";
        default: return "none";
    }
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
        logger.log(LogModule::FlashCommitCoordinator, "queue refused: reset_config (busy %u)\n",
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
        logger.log(LogModule::FlashCommitCoordinator, "queue refused: reset_programs (busy %u)\n",
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
        logger.log(LogModule::FlashCommitCoordinator, "queue refused: delete_program %u (busy %u)\n",
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

    // Program-running pause is owned by Core::update (tick is not called while PE runs).

    if (deferredPostedConfigApply_) {
        core.feedWatchdog();
        espHalFeedWdt();
        logger.log(LogModule::FlashCommitCoordinator, "Config: deferred apply start %s (heap=%u)\n",
                   HttpPostJson::TMP_CONFIG, espHalFreeHeap());
        const uint16_t beforeCrc = config.getCRC();
        const bool cfgOk =
            config.applyPostedConfigJsonFile(HttpPostJson::TMP_CONFIG, core.getSensors());
        core.feedWatchdog();
        espHalFeedWdt();

        if (!cfgOk) {
            recordFlashCommitSnapshot(FlashCommitOp::SAVE_CONFIG, false, false, config.getCRC());
            logger.log(LogModule::FlashCommitCoordinator, "Config: apply failed\n");
            webServer.broadcastStatusForce();
        } else {
            const uint16_t afterCrc = config.getCRC();
            const bool changed = (beforeCrc != afterCrc);
            recordFlashCommitSnapshot(FlashCommitOp::SAVE_CONFIG, true, changed, afterCrc);
            core.setRebootRequired(true);
            logger.log(LogModule::FlashCommitCoordinator, "Config: /config.json written (changed=%d crc:%u->%u heap=%u)\n",
                       (int)changed,
                       (unsigned)beforeCrc,
                       (unsigned)afterCrc,
                       espHalFreeHeap());
            webServer.broadcastStatusForce();
        }
        deferredPostedConfigApply_ = false;
        return;
    }

    if (deferredPostedProgramApply_) {
        core.feedWatchdog();
        espHalFeedWdt();

        if (!deferredPostedProgramIndexPhase_) {
            logger.log(LogModule::FlashCommitCoordinator, "program: deferred apply start %s (heap=%u)\n",
                       HttpPostJson::TMP_PROGRAM, espHalFreeHeap());
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
            } else {
                logger.log(LogModule::FlashCommitCoordinator, "program: JSON OK id=%u, file written, scheduling rebuildProgramIndex\n",
                           (unsigned)postedId);
                deferredPostedProgramIndexPhase_ = true;
                webServer.broadcastStatusForce();
                core.feedWatchdog();
                espHalFeedWdt();
                return; // next tick will rebuild index
            }
        } else {
            logger.log(LogModule::FlashCommitCoordinator, "program: deferred rebuildProgramIndex (heap=%u)\n", espHalFreeHeap());
            const bool ok = config.rebuildProgramIndex();
            recordFlashCommitSnapshot(FlashCommitOp::SAVE_PROGRAM, ok, ok, config.getCRC());
            logger.log(LogModule::FlashCommitCoordinator, "program: flash save %s (heap=%u)\n", ok ? "OK" : "FAILED", espHalFreeHeap());
            webServer.broadcastStatusForce();
            deferredPostedProgramApply_ = false;
            deferredPostedProgramIndexPhase_ = false;
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
        logger.log(LogModule::FlashCommitCoordinator, "deferred delete_program: id=%u\n", (unsigned)pendingProgramId_);
        ok = config.deleteProgram(pendingProgramId_);
        flashOp = FlashCommitOp::DELETE_PROGRAM;
    } else if (op == PendingFsOp::RESET_CONFIG) {
        logger.log(LogModule::FlashCommitCoordinator, "deferred reset_config\n");
        ok = config.reset();
        flashOp = FlashCommitOp::RESET_CONFIG;
        if (ok) core.setRebootRequired(true);
    } else if (op == PendingFsOp::RESET_PROGRAMS) {
        logger.log(LogModule::FlashCommitCoordinator, "deferred reset_programs\n");
        ok = config.resetPrograms();
        flashOp = FlashCommitOp::RESET_PROGRAMS;
    }
    recordFlashCommitSnapshot(flashOp, ok, ok, config.getCRC());
    logger.log(LogModule::FlashCommitCoordinator, "deferred FS op: %u done ok=%d crc=%u heap=%u\n",
               (unsigned)op, (int)ok, (unsigned)lastFlashCrc_, espHalFreeHeap());
}

