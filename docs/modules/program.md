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
- Рантайм-исполнение использует **компактные шаги**: `CompiledStep` (POD, без heap).
- `CompiledStep[Limits::MAX_STEPS_PER_PROGRAM]` и `ActionId[]` — члены `ProgramExecutor`
  (фиксированные массивы, без heap и без PoolManager).
- Строковые поля шага (`action`, `comparison`, `sensor_name`) остаются в JSON на диске и/или в UI.
- Исключение: `CompiledStep::message` — фиксированный буфер для `SMS_OWNER` (≤64 символа ASCII/GSM-7).

## Действия связи (v1)

| Action | Параметры | Поведение |
|--------|-----------|-----------|
| `CALL_OWNER` | `ms` (длительность гудков; UI в секундах) | Набрать `gsm.owner_phone`, ждать до `ms` или `NO CARRIER`/`BUSY`, затем `ATH`. MQTT CIP на эпоху выключен. |
| `SMS_OWNER` | `message` | SMS на `owner_phone` с текстом шага. |

Пустой `owner_phone` / отказ GSM → программа завершается с fail.

Входящий DTMF-старт (не шаг программы): см. [`gsm_modem.md`](gsm_modem.md) § Voice/SMS.

