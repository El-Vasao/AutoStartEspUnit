# `fs`: LittleFS, атомарность и deferred операции

## Роль
`FSManager` — единственная точка доступа к LittleFS, которая обеспечивает:
- атомарные записи (temp → rename/replace);
- ограничения по размерам “важных” файлов (через реестр метаданных);
- deferred операции, чтобы не пересекать flash с критическими участками выполнения;
- healthcheck и orphan reclaim как “страховку” после неожиданных ребутов.

Основные файлы:
- `include/fs/FSManager.h`
- `src/fs/FSManager.*.cpp`

## Контракт доступа
Правило: не использовать `LittleFS.*` напрямую из модулей приложения. Всё через `fileSystem`.
Исключение: если библиотека требует `FS&` — использовать `fileSystem.webFs()`.

## Deferred flash
Зачем: не пересекать flash-commit с hot-path (программа / SoftAP loop) на ESP32-C3.

Типовой сценарий (web):
1) web принимает POST и пишет tmp-файл (`/__http/*.tmp`)
2) web отмечает “pending”
3) в `Core::update()` вызывается `webServer.processDeferred()`
4) `FSManager` / `FlashCommitCoordinator` выполняет commit, когда это безопасно

## Атомарные записи
Общий принцип: всегда иметь возможность восстановиться после ребута в середине записи.
Используются temp файлы рядом с целевым, затем rename/replace (с учётом особенностей LittleFS).

## `gc()` на ESP32
Arduino LittleFS **не** даёт `LittleFS.gc()`. `FSManager::gc()` = reclaim известных orphan temp
(`/__http/*.tmp`, `/update.bin.tmp`) + `refreshFsInfo()`. После вызова всегда проверяйте `getFreeSpace()`.

## Инварианты по памяти
- большие копирования делаются чанками через стековый буфер `PoolLimits::FS_SCRATCH_BYTES`
- сериализация JSON в файл выполняется потоково (см. `FSManager.Json.cpp`)

