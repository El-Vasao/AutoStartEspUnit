#pragma once

#include <stdint.h>

class Core;

/// Result of last completed flash commit operation (for UI).
enum class FlashCommitOp : uint8_t {
    NONE = 0,
    SAVE_CONFIG,
    RESET_CONFIG,
    SAVE_PROGRAM,
    DELETE_PROGRAM,
    RESET_PROGRAMS
};

/// Deferred FS operation posted from HTTP (non-POST tmp).
enum class PendingFsOp : uint8_t {
    NONE = 0,
    DELETE_PROGRAM,
    RESET_CONFIG,
    RESET_PROGRAMS
};

/**
 * @brief Orchestrates deferred flash commits (LittleFS + config/program writes).
 *
 * Owned by the application/core. Web posts only set flags/queue ops;
 * actual flash work is executed from `Core::update()` when it is safe.
 */
class FlashCommitCoordinator {
public:
    void deferPostedConfigApply() { deferredPostedConfigApply_ = true; }

    void deferPostedProgramApply() {
        deferredPostedProgramApply_ = true;
        deferredPostedProgramIndexPhase_ = false;
    }

    void queueResetConfig();
    void queueResetPrograms();
    void queueDeleteProgram(uint8_t id);

    bool isPending() const {
        return deferredPostedConfigApply_ || deferredPostedProgramApply_ || pendingFsOp_ != PendingFsOp::NONE;
    }

    const char* getCurrentFlashOpString() const;

    FlashCommitOp getLastFlashOp() const { return lastFlashOp_; }
    bool getLastFlashOk() const { return lastFlashOk_; }
    uint32_t getLastFlashMillis() const { return lastFlashMillis_; }
    bool getLastFlashChanged() const { return lastFlashChanged_; }
    uint16_t getLastFlashCrc() const { return lastFlashCrc_; }

    /// Perform at most one step; designed to be called from `Core::update()`.
    void tick(Core& core);

private:
    bool deferredPostedConfigApply_{false};
    bool deferredPostedProgramApply_{false};
    bool deferredPostedProgramIndexPhase_{false};

    PendingFsOp pendingFsOp_{PendingFsOp::NONE};
    uint8_t pendingProgramId_{0};

    FlashCommitOp lastFlashOp_{FlashCommitOp::NONE};
    bool lastFlashOk_{true};
    uint32_t lastFlashMillis_{0};
    bool lastFlashChanged_{false};
    uint16_t lastFlashCrc_{0};

    void recordFlashCommitSnapshot(FlashCommitOp op, bool ok, bool changed, uint16_t crc);
};

extern FlashCommitCoordinator flashCommit;

