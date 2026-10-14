// include/io/RelayController.h
#pragma once

#include <Arduino.h>
#include "common/Pins.h"
#include "common/Constants.h"

// Пины реле больше не объявляем отдельно — они в Pins.h

struct esp_timer;

/**
 * @brief Контроллер управления реле.
 * Обеспечивает включение/выключение реле, контроль максимального времени работы.
 *
 * Инварианты:
 * - индексация во всём проекте: публичные методы принимают индекс 0..MAX_RELAYS-1.
 *   В конфиге и UI чаще используется 1..N — при конвертации не перепутайте.
 *
 * Безопасность стартера:
 * - `checkSafety()` выключает стартер после `starter_max_time_sec` всегда, кто бы его ни включил;
 *   отсчёт идёт от перехода выкл→вкл (повторный `on()` его не продлевает);
 * - страховка на esp_timer выключает пин стартера через max + `HW_BACKSTOP_MARGIN_MS`,
 *   даже если основной цикл завис; `update()` затем приводит состояние в порядок;
 * - после любого аварийного выключения стартер заблокирован на max(пауза между попытками, 5 с).
 */
class RelayController {
public:
    RelayController();

    // Инициализация пинов, установка максимального времени из конфига
    void begin();

    // Проверка таймаутов (вызывается в loop)
    void update();

    // Включить реле (индекс с 0). false — включение запрещено (блокировка стартера)
    bool on(uint8_t relay);

    // Индекс реле стартера или -1
    int8_t starterIndex() const { return _starterIdx; }

    // Стартер заблокирован после аварийного выключения
    bool isStarterLockedOut() const;

    // Выключить реле
    void off(uint8_t relay);

    // Переключить состояние реле
    void toggle(uint8_t relay);

    // Выключить все реле
    void allOff();

    // Включить все реле
    void allOn();

    // Установить состояние по битовой маске
    void setMask(uint8_t mask);

    // Получить состояние конкретного реле
    bool getState(uint8_t relay) const;

    // Получить битовую маску состояний
    uint8_t getMask() const { return _state; }

private:
    uint8_t _state;                             ///< битовая маска текущего состояния
    uint32_t _maxOnTime[HardwareLimits::RELAYS];    ///< максимальное время работы (0 = без ограничения)
    uint32_t _turnOnTime[HardwareLimits::RELAYS];   ///< момент включения (0 = выключено)

    int8_t _starterIdx{-1};
    bool _lockout{false};
    uint32_t _lockoutUntilMs{0};
    esp_timer* _backstop{nullptr};
    volatile bool _backstopTripped{false};

    // Получить номер пина для реле (использует Pin::RELAY_PINS)
    uint8_t pinForRelay(uint8_t relay) const;

    // Установить состояние реле (низкоуровнево)
    void setRelay(uint8_t relay, bool state);

    // Проверить превышение времени работы
    void checkSafety();

    void startLockout();
    void armBackstop();
    void disarmBackstop();
    static void backstopCallback(void* arg);
};

