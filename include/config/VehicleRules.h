// include/config/VehicleRules.h
#pragma once

#include "config/ConfigTypes.h"

/**
 * @brief Правила секции vehicle (стартер, алгоритм запуска) без зависимостей от железа.
 *
 * - `normalize` — путь загрузки: значения вне диапазона заменяются умолчанием, перекрёстные
 *   нарушения исправляются в RAM. Конфиг никогда не сбрасывается к заводскому из-за этих правил.
 * - `validate` — строгая проверка при сохранении настроек из веб-интерфейса.
 */
namespace vehicle_rules {

/// Timed-стартер: двигатель схватился при напряжении не ниже порога «заведён».
inline bool engineCaughtAtVoltage(float v, float runningVoltage) { return v >= runningVoltage; }

/// Флаг двигателя: выше «заведён» — true, не выше «заглушен» — false, полоса — без изменений.
inline void applyEngineRunningHysteresis(bool& running, float v, float runningVoltage,
                                         float stoppedVoltage) {
    if (v >= runningVoltage) running = true;
    else if (v <= stoppedVoltage) running = false;
}

const char* startMethodToString(StartMethod m);
StartMethod startMethodFromString(const char* s, StartMethod def);

/// Индекс входа по id или -1.
int8_t findInput(const BaseConfig& cfg, uint16_t inputId);

/// nullptr — вход подходит алгоритму запуска; иначе текст причины. Для Timed всегда nullptr.
const char* checkStartInput(const BaseConfig& cfg, StartMethod method, uint16_t inputId);

/// nullptr — вход блокировки не задан или корректен; иначе текст причины.
const char* checkBlockInput(const BaseConfig& cfg);

/// nullptr — индикатор работы двигателя настроен корректно; иначе текст причины.
const char* checkEngineIndicator(const BaseConfig& cfg);

using NoteFn = void (*)(const char* msg, void* ctx);

/// Приведение при загрузке. Каждое исправление сообщается через `note` (может быть nullptr).
/// Возвращает число исправлений.
uint8_t normalize(BaseConfig& cfg, NoteFn note, void* ctx);

/// Строгая проверка. nullptr — всё корректно; иначе текст первой найденной причины.
const char* validate(const BaseConfig& cfg);

/// Конфиг термостата полон и согласован — единственный источник для *Allowed / enable gate.
bool thermostatReady(const ThermostatConfig& ts);

/// Конфиг защиты АКБ полон и согласован — единственный источник для *Allowed / enable gate.
bool batterySaverReady(const BatterySaverConfig& bs);

} // namespace vehicle_rules
