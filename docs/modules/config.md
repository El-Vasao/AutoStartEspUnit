# `config`: базовая конфигурация и “программы”

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

## Контракты
- В RAM всегда живёт `BaseConfig` (без `String`, только фиксированные `char[]`).
- `Config::load()` заполняет `baseCache`, валидирует и нормализует.
- `Config::save()` пишет текущий `baseCache` атомарно.
- POST из web пишет tmp-файл, а “применение” делает `Config`, обычно в deferred фазе.

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

