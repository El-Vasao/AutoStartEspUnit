# Фронтенд

UI SoftAP: сторы Alpine.js 3, Ajv 2020-12, SSE. Порядок бандла — `JS_LIBS` в `build.py`. Используются плагины Alpine `focus` и `collapse` (`x-trap`, `x-collapse`). `Alpine.start` ставится микрозадачей после того, как склеенный бандл регистрирует слушателей `alpine:init`.

```mermaid
flowchart LR
  boot[index.html] --> appjs[/app.js.gz]
  appjs --> alpineInit[alpine:init]
  alpineInit --> stores[APP.stores.init]
  alpineInit --> checklist[core.js HTTP]
  checklist --> sse[EventSource /events]
  sse --> deviceStatus[стор deviceStatus]
```

## Сторы

Регистрируются в `data/js/app/stores/stores-init.js`. Уведомления: `uiToast`. Пути: `APP.path`. Ajv: `APP.validation.createAjv()`.

## SSE

`data/js/app/sse.js`: детектор зависания смотрит `lastEventAt` или `connectingSince`; при возврате видимости переподключает CONNECTING/CLOSED; `resync` заново запрашивает `/bootstrap`.

## Сохранение

Настройки: уходит каждое поле схемы (`showIf` только управляет показом). Перекрёстные правила автомобиля считаются на лету и при сохранении. Программы: `validateProgramDataHard` при сохранении и при живой проверке.

## Схемы

`data/validation/*.json` и `data/ui/*.json` заранее вложены в бандл (`PRELOADED_JSON_FILES`).
