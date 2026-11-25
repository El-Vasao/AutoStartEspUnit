# `config`: базовая конфигурация и “программы”

```mermaid
flowchart LR
  Json[/config.json/] --> Sax[Base SAX]
  Sax --> Ram[BaseConfig RAM]
  Ram --> Emit[JSON emit]
  Emit --> Atomic[FsManager atomic write]
```

## Роль
`Config` — единый источник конфигурации в RAM (`BaseConfig`) + логика хранения в LittleFS.

Основные файлы:
- `include/config/Config.h`
- `include/config/ConfigTypes.h`
- `src/config/Config.*.cpp`

## Что хранится
- `/config.json`: базовый конфиг устройства (кэшируется в RAM как `BaseConfig`).
- `/programs/<id>.json`: файлы программ.
- `/programs/index.json`: индекс программ (для быстрых списков/валидации ссылок).
- Пустой `/programs` при `ensureProgramIndex()` → заводской набор (1 «Запуск», 2 «Стоп», 230–249).

## Контракты
- В RAM всегда живёт `BaseConfig` (без `String`, только фиксированные `char[]`).
- `Config::load()` заполняет `baseCache` и прогоняет `vehicle_rules::normalize`: подставляет допустимые значения и всё равно даёт устройству загрузиться. Строгая `validate` выполняется на POST и может отвергнуть тело.
- Плохой `start_block_input_id` при загрузке не очищается: пуск двигателя остаётся заперт, режим при этом выбирается.
- `thermostat.comparison` хранится и менеджером не читается. `enabled` термостата/АКБ копируется в рантайм на boot и при выходе из OTA только если `ready` (`vehicle_rules::*Ready`); триггеры копируют `enabled` как раньше. Save с `enabled=true` без `ready` отклоняется. Пороги читаются на каждом `update()`. Сохранение настроек ставит `rebootRequired`.
- `Config::save()` пишет текущий `baseCache` атомарно.
- POST из web кладёт тело в RAM. Цикл пишет tmp, а применение делает `Config` в deferred-фазе.

## Программы: загрузка для рантайма
- Для экономии baseline RAM и уменьшения heap-рисков `Config` загружает программу для исполнения **сразу**
  в компактное представление шагов (`CompiledStep[]`), без удержания `Program{Step[]}` в статике.
- Исполнитель (`ProgramExecutor`) получает:
  - имя программы (для UI/логов),
  - `CompiledStep[Limits::MAX_STEPS_PER_PROGRAM]` и количество шагов.

## Политика памяти
- JSON разбирается SAX-парсером (`JsonStreamingParser`) в фиксированные `char[]`, без `DynamicJsonDocument`.
- Лимиты строк и ёмкости JSON заданы в `include/common/Constants.h`.

## Типичные ошибки и “острые углы”
- увеличили структуру `BaseConfig` → вырос постоянный RAM (проверять размер, лимиты и нужность полей);
- добавили `String` в конфиг → риск фрагментации heap;
- изменили поля структур → нужно синхронизировать:
  - кодек/сериализацию (`Config.*`),
  - `DefaultConfig`,
  - web UI схему/формы.

