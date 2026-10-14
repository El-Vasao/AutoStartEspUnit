// include/program/StarterMachine.h
#pragma once

#include <stdint.h>

#include "config/ConfigTypes.h"

/**
 * @brief Автомат шага ENGINE_START без зависимостей от железа.
 *
 * Исполнитель на каждом проходе снимает показания (`Sample`), вызывает `tick()` и выполняет
 * команды из `Tick`: реле стартера, сброс окна напряжения, итог шага.
 *
 * Попытка:
 * - перед прокруткой: двигатель уже работает → успех без прокрутки;
 * - прокрутка до первого из: условие (rpm/input), время прокрутки (timed), максимум;
 * - rpm/input: условие выполнено → успех сразу;
 * - timed: пауза `settleMs`, затем полное окно напряжения (не дольше `windowExtraMs`),
 *   успех при напряжении ≥ порога работы;
 * - неудача: пауза `restMs` и следующая попытка, либо итог Failure.
 * Активный вход блокировки на любом проходе — реле выключается, итог Blocked.
 */
namespace starter {

enum class Phase : uint8_t { Idle, Cranking, Settling, Resting };

enum class StopReason : uint8_t {
    None,
    AlreadyRunning,
    Condition,
    MaxTime,
    VoltageOk,
    VoltageLow,
    NoVoltage,
    Blocked,
};

enum class Outcome : uint8_t { Running, Success, Failure, Blocked };

enum class RelayCmd : uint8_t { None, On, Off };

struct Params {
    StartMethod method{StartMethod::Timed};
    uint32_t crankMs{2000};
    uint32_t maxMs{5000};
    uint32_t settleMs{10000};
    uint32_t windowExtraMs{2000};
    uint32_t restMs{5000};
    uint8_t attempts{3};
    float runningVoltage{13.2f};
};

struct Sample {
    uint32_t nowMs{0};
    bool blocked{false};
    bool condition{false};          ///< rpm: обороты подтверждены; input: вход активен
    bool voltageValid{false};
    bool voltageWindowFull{false};
    float voltage{0.0f};
};

/// Сводка последней завершённой попытки (для журнала).
struct Attempt {
    uint8_t number{0};
    StopReason reason{StopReason::None};
    uint32_t crankMs{0};
    bool hasMinVoltage{false};
    float minVoltage{0.0f};
    float finalVoltage{0.0f};
    uint32_t maxGapMs{0};           ///< наибольший промежуток между проходами во время прокрутки
};

struct Tick {
    RelayCmd relay{RelayCmd::None};
    Outcome outcome{Outcome::Running};
    bool resetVoltageWindow{false};
    bool attemptEnded{false};       ///< в `last()` свежая сводка
};

class Machine {
public:
    void begin(const Params& p, uint32_t nowMs);
    Tick tick(const Sample& s);

    Phase phase() const { return phase_; }
    uint8_t attempt() const { return attempt_; }
    const Attempt& last() const { return last_; }
    const Params& params() const { return p_; }

private:
    bool alreadyRunning(const Sample& s) const;
    void endAttempt(StopReason reason, const Sample& s, Tick& t);
    void failOrRest(const Sample& s, Tick& t);

    Params p_{};
    Phase phase_{Phase::Idle};
    uint8_t attempt_{0};
    uint32_t phaseStartMs_{0};
    uint32_t crankMs_{0};
    uint32_t lastTickMs_{0};
    bool haveTick_{false};
    bool haveMin_{false};
    float minVoltage_{0.0f};
    uint32_t maxGapMs_{0};
    Attempt last_{};
};

const char* stopReasonToString(StopReason r);

} // namespace starter
