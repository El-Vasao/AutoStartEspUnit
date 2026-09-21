# `program`: ProgramExecutor и шаги программ

## Роль
`ProgramExecutor` исполняет сценарии (“программы”), которые состоят из шагов (`Step`) и выполняют действия
над IO и логикой устройства (реле, ожидания, проверки).

Основные файлы:
- `include/program/ProgramExecutor.h`
- `src/program/ProgramExecutor.*.cpp`
- internal: `src/program/internal/ProgramExecutorInternal.h`

## Контракты
- Исполнение программ должно быть неблокирующим (таймеры/состояния вместо долгих delay).
- Старт новой программы может прервать текущую (конкуренция триггеров).
- Любые операции, требующие flash, не должны выполняться во время активного выполнения программы
  (см. “deferred flash” в FS/web/core).

## Память
- Рантайм-исполнение использует **компактные шаги без строк**: `CompiledStep`.
- `CompiledStep[Limits::MAX_STEPS_PER_PROGRAM]` и `ActionId[]` — члены `ProgramExecutor`
  (фиксированные массивы, без heap и без PoolManager).
- Строковые поля шага (`action`, `comparison`, `sensor_name`) остаются только в JSON на диске и/или в UI,
  но **не хранятся в hot-path** выполнения.

