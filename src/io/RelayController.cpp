/**
 * @file RelayController.cpp
 * @brief Реализация подсистемы IO: управление реле и ограничения безопасности (таймауты).
 *
 * Инварианты:
 * - Управление реле должно быть детерминированным и быстрым (без heap/`String`).
 * - Проверка безопасности выполняется регулярно в `update()` и может форсировать OFF по таймауту.
 *
 * Память:
 * - Состояние хранится компактно: битмаска + массивы таймеров фиксированной длины.
 *
 * Запрещено:
 * - Делать блокирующие задержки при переключении реле.
 * - Включать реле из ISR/таймера (страховка esp_timer только выключает пин стартера).
 */
#include "io/RelayController.h"
#include "core/Core.h"
#include "config/Config.h"
#include "core/ErrorManager.h"
#include "common/Utils.h"
#include "common/Logger.h"
#include "common/Constants.h"
#include "io/HwMap.h"

#include <esp_timer.h>

RelayController::RelayController() : _state(0) {
    memset(_maxOnTime, 0, sizeof(_maxOnTime));
    memset(_turnOnTime, 0, sizeof(_turnOnTime));
}

void RelayController::begin() {
    logger.log(LogModule::RelayController, "start\n");
    for (int i = 0; i < HardwareLimits::RELAYS; i++) {
        uint8_t pin = pinForRelay(i);
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    
    for (int i = 0; i < HardwareLimits::RELAYS; i++) {
        if (digitalRead(pinForRelay(i)) == HIGH) {
            _state |= (1 << i);
            _turnOnTime[i] = millis();
        }
    }
    
    applyStarterConfig();
    logger.log(LogModule::RelayController, "ok\n");
}

void RelayController::applyStarterConfig() {
    const uint16_t starterRelayId = config.getBase().vehicle.starter_relay_id;
    const int8_t newIdx = HwMap::relayIndexById(starterRelayId);
    uint16_t starterTime = config.getBase().vehicle.starter_max_time_sec;
    if (starterTime == 0) starterTime = StarterLimits::MAX_TIME_DEFAULT_SEC;
    const uint32_t maxMs = secToMs(starterTime);

    // Пока стартерный пин ещё под током — не менять индекс: иначе страховка режет старый пин,
    // а катушка на новом остаётся без лимита.
    const bool starterEnergized = (_starterIdx >= 0 && getState((uint8_t)_starterIdx));
    if (starterEnergized && newIdx != _starterIdx) {
        _starterRebindPending = true;
        logger.log(LogModule::RelayController,
                   "starter rebind deferred: relay still on (old idx=%d, new id=%u)\n",
                   (int)_starterIdx, (unsigned)starterRelayId);
        if (_starterIdx >= 0) {
            _maxOnTime[(uint8_t)_starterIdx] = maxMs;
            if (getState((uint8_t)_starterIdx)) armBackstop();
        }
        return;
    }

    if (_starterIdx >= 0 && _starterIdx != newIdx) {
        _maxOnTime[(uint8_t)_starterIdx] = 0;
        disarmBackstop();
    }
    _starterIdx = newIdx;
    _starterRebindPending = false;
    if (_starterIdx >= 0) {
        _maxOnTime[(uint8_t)_starterIdx] = maxMs;
        logger.log(LogModule::RelayController, "starter relay relay_id: %u (idx=%d), max time: %u ms\n",
                   (unsigned)starterRelayId, (int)_starterIdx, (unsigned)maxMs);
        if (!_backstop) {
            esp_timer_create_args_t args{};
            args.callback = &RelayController::backstopCallback;
            args.arg = this;
            args.dispatch_method = ESP_TIMER_TASK;
            args.name = "starter_backstop";
            if (esp_timer_create(&args, &_backstop) != ESP_OK) {
                _backstop = nullptr;
                logger.log(LogModule::RelayController, "⚠️ failed to create starter watchdog timer\n");
            }
        }
        if (getState((uint8_t)_starterIdx)) armBackstop();
    }
}

uint8_t RelayController::pinForRelay(uint8_t relay) const {
    if (relay >= HardwareLimits::RELAYS) return 0;
    return Pin::RELAY_PINS[relay];
}

void RelayController::setRelay(uint8_t relay, bool state) {
    if (relay >= HardwareLimits::RELAYS) return;
    const bool wasOn = getState(relay);
    const bool isStarter = (_starterIdx >= 0 && relay == (uint8_t)_starterIdx);
    if (state) {
        if (wasOn) return;
        digitalWrite(pinForRelay(relay), HIGH);
        _state |= (1 << relay);
        _turnOnTime[relay] = millis();
        if (isStarter) armBackstop();
    } else {
        digitalWrite(pinForRelay(relay), LOW);
        _state &= ~(1 << relay);
        _turnOnTime[relay] = 0;
        if (isStarter) disarmBackstop();
    }
}

bool RelayController::isStarterLockedOut() const {
    if (_backstopTripped) return true;
    return _lockout;
}

bool RelayController::on(uint8_t relay) {
    if (relay >= HardwareLimits::RELAYS) return false;
    if (_starterIdx >= 0 && relay == (uint8_t)_starterIdx && isStarterLockedOut()) {
        logger.log(LogModule::RelayController, "starter enable rejected: locked after protection trip\n");
        return false;
    }
    setRelay(relay, true);
    return true;
}

void RelayController::off(uint8_t relay) {
    setRelay(relay, false);
}

void RelayController::toggle(uint8_t relay) {
    if (relay >= HardwareLimits::RELAYS) return;
    if (getState(relay)) off(relay);
    else on(relay);
}

void RelayController::allOff() {
    for (int i = 0; i < HardwareLimits::RELAYS; i++) {
        off(i);
    }
}

bool RelayController::getState(uint8_t relay) const {
    if (relay >= HardwareLimits::RELAYS) return false;
    return (_state >> relay) & 1;
}

void RelayController::startLockout() {
    const uint32_t pauseMs = secToMs(config.getBase().vehicle.starter_retry_pause_sec);
    const uint32_t lockMs = pauseMs > StarterLimits::LOCKOUT_MIN_MS ? pauseMs : StarterLimits::LOCKOUT_MIN_MS;
    _lockout = true;
    _lockoutStartMs = millis();
    _lockoutDurationMs = lockMs;
    logger.log(LogModule::RelayController, "starter locked for %u ms\n", (unsigned)lockMs);
}

void RelayController::armBackstop() {
    if (!_backstop || _starterIdx < 0) return;
    esp_timer_stop(_backstop);
    const uint64_t us =
        (uint64_t)(_maxOnTime[(uint8_t)_starterIdx] + StarterLimits::HW_BACKSTOP_MARGIN_MS) * 1000ULL;
    esp_timer_start_once(_backstop, us);
}

void RelayController::disarmBackstop() {
    if (_backstop) esp_timer_stop(_backstop);
}

void RelayController::backstopCallback(void* arg) {
    auto* self = static_cast<RelayController*>(arg);
    if (!self || self->_starterIdx < 0) return;
    digitalWrite(Pin::RELAY_PINS[(uint8_t)self->_starterIdx], LOW);
    self->_backstopTripped = true;
}

void RelayController::checkSafety() {
    uint32_t now = millis();
    for (int i = 0; i < HardwareLimits::RELAYS; i++) {
        if (getState(i) && _maxOnTime[i] > 0) {
            if (now - _turnOnTime[i] > _maxOnTime[i]) {
                // Страховка: реле «не должно» оставаться включённым дольше лимита.
                // Особенно критично для стартера/нагревателей — принудительно выключаем и фиксируем ошибку.
                logger.log(LogModule::RelayController, "⚠️ relay %d timeout! Forcing OFF\n", i+1);
                setRelay(i, false);
                core.getErrorManager().set(ErrorCode::RELAY_TIMEOUT);
                if (_starterIdx >= 0 && i == _starterIdx) startLockout();
            }
        }
    }
}

void RelayController::update() {
    if (_lockout && (millis() - _lockoutStartMs) >= _lockoutDurationMs) {
        _lockout = false;
    }
    if (_backstopTripped) {
        if (_starterIdx >= 0) setRelay((uint8_t)_starterIdx, false);
        logger.log(LogModule::RelayController, "⚠️ starter watchdog fired (main loop hung)\n");
        core.getErrorManager().set(ErrorCode::RELAY_TIMEOUT);
        startLockout();
        _backstopTripped = false;
    }
    if (_starterRebindPending && (_starterIdx < 0 || !getState((uint8_t)_starterIdx))) {
        applyStarterConfig();
    }
    checkSafety();
}
