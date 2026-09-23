# `core`: orchestration, режимы, периодика

## Роль
`core` — единственная подсистема, которая “видит всю картину” и управляет жизненным циклом устройства.

Контракт:
- `setup()` вызывает `core.begin()`;
- `loop()` вызывает `core.update()` и больше ничего.

Основные файлы:
- `include/core/Core.h`
- `src/core/Core.Core.cpp`, `src/core/Core.Modes.cpp`, `src/core/Core.Timing.cpp`, `src/core/Core.Diagnostics.cpp`
- `include/core/ModeManager.h`, `src/core/Core.ModeManager.cpp`
- internal хранилище: `src/core/internal/CorePrivate.h`

## Обязанности `Core`
- инициализация подсистем (FS, config, IO, managers);
- выбор стартового режима (NORMAL/SETUP_AP/EMERGENCY_AP);
- единый “тик” системы (`update()`): watchdog, периодика, обработка текущего режима;
- координация deferred flash операций (через web/fs) так, чтобы они не пересекались с выполнением программ.

## Режимы (`CoreMode`)
Режимы определены в `include/common/Constants.h`:
- `BOOT`
- `EMERGENCY_AP`
- `SETUP_AP`
- `NORMAL`
- `NORMAL_SILENT`
- `OTA_UPDATE`

`ModeManager` отвечает за enter/exit и включение подсистем (web/ota), а доменная логика и hot-path update’ы живут в
`Core::handle*()`.

`NORMAL` / `NORMAL_SILENT`: одинаковый доменный стек + GSM/MQTT; silent только без SoftAP. Wake в NORMAL: кнопка IN3 или MQTT `set` `wifi_ap`. `OTA_UPDATE`: на enter гасятся programs/relays и runtime triggers/battery/thermostat.

## Важные инварианты
- `Core::update()` должен быть неблокирующим и вызываться часто.
- Долгие операции обязаны делать time slicing:
  - `Core::cooperate()` — throttled `yield()` для FreeRTOS/lwIP fairness
  - `Core::feedWatchdog()` — для длительных операций, где частый `yield()` вреден

## “Runtime knobs”
`Core` предоставляет узкие методы для рантайм-управления менеджерами (например включить/выключить термостат/триггеры).
Правило: такие методы должны **делегировать** в соответствующий manager и не разрастаться в бизнес-логику.

