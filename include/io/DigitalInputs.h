// include/io/DigitalInputs.h
#pragma once

#include <Arduino.h>
#include "common/Constants.h"
#include "io/RpmEstimator.h"

// Структура для хранения состояния входа с антидребезгом
struct DebouncedInput {
    uint8_t pin;                ///< номер пина
    bool currentState;          ///< текущее стабильное состояние (логическое)
    bool lastRawState;          ///< последнее сырое состояние с пина
    uint32_t lastChangeTime;    ///< время последнего изменения (мс)
    bool stable;                ///< флаг стабильности (после дребезга)

    DebouncedInput() : pin(0), currentState(HIGH), lastRawState(HIGH),
                       lastChangeTime(0), stable(true) {}
};

/**
 * @brief Обработка цифровых входов (антидребезг, счётчики импульсов).
 *
 * Инварианты/заметки:
 * - `getState()` возвращает “логическое активное состояние” с учётом `active_state` из конфига.
 * - для `PULSE_COUNTER` физического “уровня” нет: `getState()` = подтверждённое превышение `threshold_rpm`
 *   (держится непрерывно `Rpm::CONFIRM_MS`); без порога всегда false.
 * - ISR ловит каждый CHANGE и пишет время перепада в `RpmEstimator`, поэтому `pulses_per_rev`
 *   в конфиге — число перепадов (оба фронта) на оборот.
 *
 * Ограничение реализации:
 * - для счётчика импульсов есть обработчики только под 3 входа (handleInterrupt0..2).
 *   При увеличении `HardwareLimits::INPUTS` нужно добавить новые обработчики и ветки attachCounterInterrupt().
 */
class DigitalInputs {
public:
    DigitalInputs();

    // Настройка пинов и прерываний (вызывается один раз)
    void begin();

    // Обновление состояния (должно вызываться часто)
    void update();

    // ---- Состояние (логическое, с учётом active_state) ----

    // Состояние входа (с учётом active_state)
    bool getState(uint8_t index) const;

    // Физический уровень на пине (без учёта active_state)
    bool getRawState(uint8_t index) const;

    // Флаг стабильности (после антидребезга)
    bool isStable(uint8_t index) const;

    // ---- Детекция фронтов ----

    // Было ли нажатие (переход из неактивного в активное) для DIGITAL-входа
    bool wasPressed(uint8_t index);

    // Было ли отпускание (переход из активного в неактивное)
    bool wasReleased(uint8_t index);

    // Было ли срабатывание на заданный уровень
    bool wasTriggered(uint8_t index, bool level);

    // ---- Длительные нажатия ----

    // Удерживается ли кнопка дольше указанного времени (мс)
    bool isHeld(uint8_t index, uint32_t ms);

    // Время удержания (мс) для текущего нажатия
    uint32_t getHoldTime(uint8_t index) const;

    // ---- Кнопка на плате (всегда индекс 2) ----

    bool isButtonPressed() const { return getState(2) == LOW; }
    bool wasButtonPressed();
    bool wasButtonReleased();
    bool isButtonHeld(uint32_t ms);

    // ---- Для счётчика импульсов ----

    // Является ли вход счётчиком
    bool isCounter(uint8_t index) const;

    // Частота в об/мин (для счётчика)
    float getFrequency(uint8_t index) const;

    // Обороты держатся на пороге `threshold_rpm` не меньше `Rpm::CONFIRM_MS` (вход включён, счётчик)
    bool isRpmConfirmed(uint8_t index) const;

    // ---- Runtime управление ----

    // Включён ли вход в рантайме
    bool isRuntimeEnabled(uint8_t index) const;

    // Установить runtime-флаг включения
    void setRuntimeEnabled(uint8_t index, bool en);

    // Найти физический индекс входа по стабильному id из конфига (или -1)
    int8_t findIndexById(uint16_t inputId) const;

private:
    DebouncedInput inputs[HardwareLimits::INPUTS];   ///< состояние каждого входа
    bool prevStates[HardwareLimits::INPUTS];         ///< предыдущие состояния для детекции фронтов
    bool _runtimeEnabled[HardwareLimits::INPUTS];    ///< включён ли вход в рантайме

    static RpmEstimator _rpm[HardwareLimits::INPUTS]; ///< времена перепадов (пишет ISR)
    RpmConfirm _rpmConfirm[HardwareLimits::INPUTS];   ///< подтверждение порога оборотов
    float _frequencies[HardwareLimits::INPUTS];      ///< рассчитанные обороты (об/мин)
    uint32_t _lastCalcTime[HardwareLimits::INPUTS];  ///< время последнего расчёта оборотов

    void resetRpm(uint8_t index);

    // Обработчики прерываний (по одному на вход)
    static void IRAM_ATTR handleInterrupt0();
    static void IRAM_ATTR handleInterrupt1();
    static void IRAM_ATTR handleInterrupt2();

    // Отключение/включение прерываний для счётчика
    void detachCounterInterrupt(uint8_t index);
    void attachCounterInterrupt(uint8_t index);
};

