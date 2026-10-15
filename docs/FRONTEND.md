# FRONTEND.md

SoftAP UI: Alpine.js 3 stores, Ajv 2020-12, SSE. Bundle order is `JS_LIBS` in `build.py`. Alpine plugins `focus` and `collapse` are used (`x-trap`, `x-collapse`). Alpine.start is queued as a microtask after the concatenated bundle registers `alpine:init` listeners.

```mermaid
flowchart LR
  boot[index.html] --> appjs[/app.js.gz]
  appjs --> alpineInit[alpine:init]
  alpineInit --> stores[APP.stores.init]
  alpineInit --> checklist[core.js HTTP]
  checklist --> sse[EventSource /events]
  sse --> deviceStatus[deviceStatus store]
```

## Stores

Registered in `data/js/app/stores/stores-init.js`. Notifications: `uiToast`. Paths: `APP.path`. Ajv: `APP.validation.createAjv()`.

## SSE

`data/js/app/sse.js`: hang detector uses `lastEventAt` or `connectingSince`; visibility reconnects CONNECTING/CLOSED; `resync` refetches `/bootstrap`.

## Save

Settings: every schema field is posted (`showIf` is display-only). Vehicle cross-rules run live and on save. Programs: `validateProgramDataHard` on save and live validate.

## Schemas

`data/validation/*.json` and `data/ui/*.json` are preloaded into the bundle (`PRELOADED_JSON_FILES`).
