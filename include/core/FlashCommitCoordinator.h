#pragma once

#include <stdint.h>

class Core;

/// Результат последней завершённой фиксации во flash (для UI).
enum class FlashCommitOp : uint8_t {
    NONE = 0,
    SAVE_CONFIG,
    RESET_CONFIG,
    SAVE_PROGRAM,
    DELETE_PROGRAM,
    RESET_PROGRAMS
};

/// Отложенная операция ФС, поставленная из HTTP (не временный файл POST).
enum class PendingFsOp : uint8_t {
    NONE = 0,
    DELETE_PROGRAM,
    RESET_CONFIG,
    RESET_PROGRAMS
};

/**
 * @brief Дирижирует отложенными фиксациями во flash (LittleFS и запись конфига/программ).
 *
 * Принадлежит приложению/ядру. Веб только ставит флаги и операции в очередь;
 * сама работа с flash выполняется из `Core::update()`, когда это безопасно.
 */
class FlashCommitCoordinator {
public:
    void deferPostedConfigApply() { deferredPostedConfigApply_ = true; }

    void deferPostedProgramApply() {
        deferredPostedProgramApply_ = true;
        deferredPostedProgramIndexPhase_ = false;
    }

    /// false, если другая операция flash уже в очереди (не перезаписывает).
    bool queueResetConfig();
    bool queueResetPrograms();
    bool queueDeleteProgram(uint8_t id);

    bool isPending() const {
        return deferredPostedConfigApply_ || deferredPostedProgramApply_ || pendingFsOp_ != PendingFsOp::NONE;
    }

    FlashCommitOp getLastFlashOp() const { return lastFlashOp_; }
    bool getLastFlashOk() const { return lastFlashOk_; }
    uint32_t getLastFlashMillis() const { return lastFlashMillis_; }

    /// Делает не больше одного шага; рассчитан на вызов из `Core::update()`.
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

