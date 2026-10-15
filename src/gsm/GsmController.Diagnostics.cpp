/**
 * @file GSMController.Diagnostics.cpp
 * @brief Диагностические функции GSMController: polling сигнала/оператора и журнал событий.
 *
 * Назначение:
 * - Содержать “побочные” диагностические операции, которые не влияют на основной FSM bring-up,
 *   и вызываются редко (READY интервалами) или по месту для трассировки событий.
 *
 * Память:
 * - Никаких динамических аллокаций. Оператор берём из ответа `AT+COPS?` / URC `+COPS:` в фиксированный `char[]`.
 *
 * Запрещено:
 * - Делать частые диагностические запросы к модему (SIM800 медленный; не спамим AT).
 * - Держать `String` как поле/кэш внутри GSMController.
 */
#include "gsm/GSMController.h"

#include "common/Logger.h"

void GSMController::ev(uint8_t type, uint16_t aux) {
    _ev[_evHead] = { millis(), type, (uint8_t)_state, aux };
    _evHead = (uint8_t)((_evHead + 1) % EVENT_RING_SIZE);
}

void GSMController::updateSignalQuality() {
    // Asynchronous poll: parse +CSQ: <rssi>,<ber> from incoming lines.
    // Avoid blocking modem polls in the READY hot-path; +CSQ is parsed from URC lines.
    sendAt("AT+CSQ", "CSQ", AwaitKind::NONE, GSM::AT_OK_TIMEOUT_MS);
}

void GSMController::readOperator() {
    // Ответ парсится в `handleUrc()` по строке `+COPS:` (без TinyGSM / без heap-String).
    sendAt("AT+COPS?", "COPS", AwaitKind::NONE, GSM::AT_OK_TIMEOUT_MS);
}
