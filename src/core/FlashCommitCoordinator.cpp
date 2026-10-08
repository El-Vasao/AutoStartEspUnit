#include "core/FlashCommitCoordinator.h"

#include "core/Core.h"
#include "program/ProgramExecutor.h"
#include "config/Config.h"
#include "fs/FSManager.h"
#include "web/WebServer.h"
#include "common/Constants.h"
#include "common/EspHal.h"
#include "common/Logger.h"

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

void FlashCommitCoordinator::queueResetConfig() {
    if (pendingFsOp_ != PendingFsOp::NONE && pendingFsOp_ != PendingFsOp::RESET_CONFIG) {
        logger.log("[FlashCommit] queue overwrite: %u -> reset_config\n", (unsigned)pendingFsOp_);
    }
    pendingFsOp_ = PendingFsOp::RESET_CONFIG;
}

void FlashCommitCoordinator::queueResetPrograms() {
    if (pendingFsOp_ != PendingFsOp::NONE && pendingFsOp_ != PendingFsOp::RESET_PROGRAMS) {
        logger.log("[FlashCommit] queue overwrite: %u -> reset_programs\n", (unsigned)pendingFsOp_);
    }
    pendingFsOp_ = PendingFsOp::RESET_PROGRAMS;
}

void FlashCommitCoordinator::queueDeleteProgram(uint8_t id) {
    if (pendingFsOp_ != PendingFsOp::NONE &&
        !(pendingFsOp_ == PendingFsOp::DELETE_PROGRAM && pendingProgramId_ == id)) {
        logger.log("[FlashCommit] queue overwrite: %u -> delete_program %u\n",
                   (unsigned)pendingFsOp_, (unsigned)id);
    }
    pendingProgramId_ = id;
    pendingFsOp_ = PendingFsOp::DELETE_PROGRAM;
}

void FlashCommitCoordinator::tick(Core& core) {
    core.feedWatchdog();
    espHalFeedWdt();

    // Program-running pause is owned by Core::update (tick is not called while PE runs).

    if (deferredPostedConfigApply_) {
        core.feedWatchdog();
        espHalFeedWdt();
        logger.log("[FlashCommit] Config: deferred apply start %s (heap=%u)\n",
                   HttpPostJson::TMP_CONFIG, espHalFreeHeap());
        const uint16_t beforeCrc = config.getCRC();
        const bool cfgOk =
            config.applyPostedConfigJsonFile(HttpPostJson::TMP_CONFIG, core.getSensors());
        core.feedWatchdog();
        espHalFeedWdt();

        if (!cfgOk) {
            recordFlashCommitSnapshot(FlashCommitOp::SAVE_CONFIG, false, false, config.getCRC());
            logger.log("[FlashCommit] Config: apply failed\n");
            webServer.broadcastStatusForce();
        } else {
            const uint16_t afterCrc = config.getCRC();
            const bool changed = (beforeCrc != afterCrc);
            recordFlashCommitSnapshot(FlashCommitOp::SAVE_CONFIG, true, changed, afterCrc);
            core.setRebootRequired(true);
            logger.log("[FlashCommit] Config: /config.json written (changed=%d crc:%u->%u heap=%u)\n",
                       (int)changed,
                       (unsigned)beforeCrc,
                       (unsigned)afterCrc,
                       espHalFreeHeap());
            webServer.broadcastStatusForce();
        }
        deferredPostedConfigApply_ = false;
    }

    if (deferredPostedProgramApply_) {
        core.feedWatchdog();
        espHalFeedWdt();

        if (!deferredPostedProgramIndexPhase_) {
            logger.log("[FlashCommit] program: deferred apply start %s (heap=%u)\n",
                       HttpPostJson::TMP_PROGRAM, espHalFreeHeap());
            uint8_t postedId = 0;
            const bool parsedOk = config.commitPostedProgramFile(HttpPostJson::TMP_PROGRAM, &postedId);
            if (!parsedOk) {
                logger.log("[FlashCommit] program: parse JSON from tmp failed\n");
                recordFlashCommitSnapshot(FlashCommitOp::SAVE_PROGRAM, false, false, config.getCRC());
                webServer.broadcastStatusForce();
                deferredPostedProgramApply_ = false;
            } else {
                logger.log("[FlashCommit] program: JSON OK id=%u, file written, scheduling rebuildProgramIndex\n",
                           (unsigned)postedId);
                deferredPostedProgramIndexPhase_ = true;
                webServer.broadcastStatusForce();
                core.feedWatchdog();
                espHalFeedWdt();
                return; // next tick will rebuild index
            }
        } else {
            logger.log("[FlashCommit] program: deferred rebuildProgramIndex (heap=%u)\n", espHalFreeHeap());
            const bool ok = config.rebuildProgramIndex();
            recordFlashCommitSnapshot(FlashCommitOp::SAVE_PROGRAM, ok, ok, config.getCRC());
            logger.log("[FlashCommit] program: flash save %s (heap=%u)\n", ok ? "OK" : "FAILED", espHalFreeHeap());
            webServer.broadcastStatusForce();
            deferredPostedProgramApply_ = false;
            deferredPostedProgramIndexPhase_ = false;
        }
    }

    if (pendingFsOp_ == PendingFsOp::NONE) return;

    const PendingFsOp op = pendingFsOp_;
    pendingFsOp_ = PendingFsOp::NONE;

    logger.log("[FlashCommit] deferred FS op: %u start (heap=%u)\n", (unsigned)op, espHalFreeHeap());
    core.feedWatchdog();
    espHalFeedWdt();
    bool ok = false;
    FlashCommitOp flashOp = FlashCommitOp::NONE;
    if (op == PendingFsOp::DELETE_PROGRAM) {
        logger.log("[FlashCommit] deferred delete_program: id=%u\n", (unsigned)pendingProgramId_);
        ok = config.deleteProgram(pendingProgramId_);
        flashOp = FlashCommitOp::DELETE_PROGRAM;
    } else if (op == PendingFsOp::RESET_CONFIG) {
        logger.log("[FlashCommit] deferred reset_config\n");
        ok = config.reset();
        flashOp = FlashCommitOp::RESET_CONFIG;
        if (ok) core.setRebootRequired(true);
    } else if (op == PendingFsOp::RESET_PROGRAMS) {
        logger.log("[FlashCommit] deferred reset_programs\n");
        ok = config.resetPrograms();
        flashOp = FlashCommitOp::RESET_PROGRAMS;
    }
    recordFlashCommitSnapshot(flashOp, ok, ok, config.getCRC());
    logger.log("[FlashCommit] deferred FS op: %u done ok=%d crc=%u heap=%u\n",
               (unsigned)op, (int)ok, (unsigned)lastFlashCrc_, espHalFreeHeap());
}

