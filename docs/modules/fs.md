# `fs`: LittleFS, атомарность и deferred операции

## Роль
`FsManager` — единственная точка доступа к LittleFS, которая обеспечивает:
- атомарные записи (temp → rename/replace);
- учёт известных путей в реестре метаданных (это не список файлов на диске: там `/config.json`, `/config.bak` и несжатые имена UI, а обязательные ассеты — `*.gz`);
- deferred операции, чтобы не пересекать flash с критическими участками выполнения;
- healthcheck и orphan reclaim как “страховку” после неожиданных ребутов.

Основные файлы:
- `include/fs/FsManager.h`
- `src/fs/FsManager.*.cpp`

## Контракт доступа
Правило: не использовать `LittleFS.*` напрямую из модулей приложения. Всё через `fileSystem`.
Исключение: если библиотека требует `FS&` — использовать `fileSystem.webFs()`.

## Отложенная запись flash
Зачем: не пересекать flash-commit с hot-path (программа / SoftAP loop) на ESP32-C3.

Типовой сценарий (web):
1) обработчик POST копирует тело в RAM (`FlashCommitCoordinator`)
2) цикл пишет `/__http/cfg.tmp` или `/__http/prg.tmp` и отмечает pending
3) в `Core::update()` вызывается `flashCommit.tick`
4) `FlashCommitCoordinator` выполняет commit, когда это безопасно

`{"success":true}` уходит, когда тело поставлено в RAM, до применения.

## Атомарные записи
Общий принцип: всегда иметь возможность восстановиться после ребута в середине записи.
Используются temp файлы рядом с целевым, затем rename/replace (с учётом особенностей LittleFS).

## `gc()` на ESP32
Arduino LittleFS **не** даёт `LittleFS.gc()`. `FsManager::gc()` снимает только два известных сироты
(`/__http/cfg.tmp` и `/__http/prg.tmp`) и вызывает `refreshFsInfo()`. После вызова всегда проверяйте `getFreeSpace()`. Успешный rename удаляет `.bak` не у всех файлов: `/programs/*.json`, кроме `index.json`, `.bak` оставляют.

OTA пишет UI-файлы в хвост `POST /upload` напрямую в LittleFS (раскладка gzip-ассетов), без промежуточного образа прошивки на диске.

## Инварианты по памяти
- большие копирования делаются чанками через стековый буфер `PoolLimits::FS_SCRATCH_BYTES`
- сериализация JSON в файл выполняется потоково (см. `FsManager.Json.cpp`)

