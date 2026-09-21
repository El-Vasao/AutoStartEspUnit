# `io`: входы, реле, сенсоры

## Роль
`io` — hot-path подсистема, которая взаимодействует с “железом”:
- цифровые входы (антидребезг, pulse counter)
- реле
- температурные датчики DS18B20 + измерение напряжения (ADC)

Основные файлы:
- `include/io/DigitalInputs.h` + `src/io/DigitalInputs.Core.cpp`
- `include/io/RelayController.h` + `src/io/RelayController.Core.cpp`
- `include/io/SensorsController.h` + `src/io/SensorsController.Core.cpp`
- hardware map: `include/common/Pins.h`

## Инварианты
- `update()` должен быть быстрым и без heap аллокаций.
- ISR (pulse counter) — без логов и тяжёлых операций, только инкремент счётчика.
- Все структуры фиксированного размера (`HardwareLimits::*`).

## Конфигурация
IO читает параметры из `config.getBase()`:
- `active_state`, `enabled`, тип входа, пороги и т.п.
Правило: `Config` хранит строки/значения в предсказуемых структурах; IO не должен зависеть от JSON.

