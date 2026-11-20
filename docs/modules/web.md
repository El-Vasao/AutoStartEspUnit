# `web`: SoftAP, HTTP API, SSE (UI)

```mermaid
flowchart LR
  AP[SoftAP] --> HTTP[ESPAsyncWebServer]
  HTTP --> API[JSON API]
  HTTP --> SSE[/events]
  HTTP --> GZ[index.html.gz style.css.gz app.js.gz]
```

## Роль

Web-подсистема — локальный UI поверх SoftAP/captive portal:

- статические файлы UI (на ФС только `*.gz`), в колбэке — из RAM, не из LittleFS;
- HTTP API для настроек и программ;
- SSE для статуса и логов.

Основные файлы:

- `include/web/WebServer.h`
- `src/web/WebServer.*.cpp`
- internal: `src/web/internal/*`
- fallback UI: `include/web/FallbackWebPage.h`

## Сессии UI

- Клиент периодически обновляет аренду (`POST /ui/session`) — для таймаута простоя SoftAP и допуска журнала в SSE.
- Фиксированный пул сессий без heap; таймаут `WebUi::SESSION_TIMEOUT_MS`, heartbeat — `WebUi::HEARTBEAT_INTERVAL_MS`.
- SSE **не требует** сессию; сессия с фронтенда — по возможности.

## Контракт по памяти (ESP32-C3)

- Лимиты: `JsonBytes::Web`, `WebSseLimits`, `APConfig` в `include/common/Constants.h`.
- JSON API body: `sendJsonBuffered` — один emit в буфер (~4 KB) + `beginResponse`.
- `String` только кратковременно (параметры AsyncWebServer).

### `/bootstrap`

Один ответ при старте UI: metadata (`version`, `uiLease`, `hwCounts`, `hwMap`, ROMs) **и** `live` snapshot.
Отдельного `/bootstrap/live` нет — дальнейшие обновления идут по SSE.

### SoftAP и сотовая связь

На ESP32-C3 SoftAP UI и GSM/MQTT **сосуществуют**. Cellular suspend при:

- режиме `OTA_UPDATE` (stream flash);
- `SETUP_AP` / `EMERGENCY_AP` (локальная настройка/recovery без модема).

В `NORMAL` и `NORMAL_SILENT` GSM/MQTT обслуживаются одинаково; silent только гасит SoftAP/веб.

Нет `noteHeavyUiTraffic` / `POST /ui/ready` / UI-storm defer / pre-OTA pressure.

Порядок FE:

1. список HTTP (`/bootstrap` → схемы → `/config/get` → `/programs`);
2. EventSource `/events` (без искусственных SoftAP gaps на ESP32-C3);
3. по возможности `POST /ui/session` → живые обновления;
4. на SSE connect — baseline burst (`clocks`→`mode`→`gsm`→`hardware` в одном тике под soft gate);
   unlock после `mode`+`hardware` (или уже из `bootstrap.live`).

## Контракт видимости UI

1. **Settings subtabs (`.settings-subtab-pane`):** видимость только через CSS
   `display:none` + класс `.active`. **Запрещён** Alpine `x-show` на тех же элементах.
2. **Main tabs (`.tab-pane`):** только Alpine `x-show` по `activeTab`.
3. **System tab:** шелл всегда в DOM после unlock; не гейтить `deviceStatus.loaded`.
4. Смена главного таба — через `uiState.setActiveTab()`.
5. **Вкладки не persist’ятся:** F5 всегда открывает «Панель».
6. **Boot UI:** `#boot-splash` / `html.ui-booting` до `unlock`/`failInit`.

## Жизненный цикл старта UI

1. **`#boot-splash`** вне `x-cloak` — виден до `unlock`/`failInit`.
2. **`initLock`** vs **`flashLock`**: `.ui-locked` = `initLock || flashLock`.
3. API: `beginInit()` → progress → `unlock()` / `failInit()`.
4. Head-loader `/app.js`: успех = `alpine:initialized`.
5. Captive `onNotFound`: отсутствующие `.js`/`.css` → **404**, не redirect `/`.
6. SSE panel-ready держит lock до `sseInitDeadlineMs` (12s), иначе degraded; safety ~20s.

## Отдача UI SoftAP (index, CSS и app.js)

1. `GET /` → `index.html.gz`
2. `GET /style.css` → `style.css.gz`
3. `GET /app.js` → `app.js.gz`
4. Sentinel `#ui-doc-complete` при неполной доставке HTML — cache-bust reload.

Обязательные UI-ассеты: `WebAssets::REQUIRED` — **на LittleFS только `*.gz`**.

Колбэк async не открывает LittleFS и не вызывает `beginResponse(FS)`:

- `POST` JSON копируется в RAM (`Limits::CONFIG_JSON_SIZE`); `FlashCommitCoordinator::tick()` пишет tmp и применяет.
- `GET /config/get` сериализует RAM-кэш `Config`.
- При старте точки в кучу кладутся `index.html.gz` (~12 КБ), `style.css.gz` (~7 КБ) и схемы до 16 КБ. `app.js.gz` (~122 КБ) целиком не держим: цикл читает окно 4 КБ, колбэк копирует его. `GET /program` и индекс — одно чтение на цикле в буфер 5 КБ. Если копия не готова или насос занят — **503**, без диска.

## SSE

- `log` — текст через `logger.log()` (при активной UI-сессии + подписчике);
- incremental status — kinds (`clocks`/`hardware`/…); connect → baseline burst (soft-gated).

Лимиты:

- `WebSseLimits::MAX_SSE_CLIENTS = 4`;
- soft queue `SSE_SOFT_QUEUE_MAX = 20`, hard `SSE_MAX_QUEUED_MESSAGES = 21` (`platformio.ini`; +1 reserved);
- при soft overflow логи копятся в счётчик дропов; перед следующей успешной отправкой — `[SSE] dropped N log messages` (если N≥1);
- период tick: `Timing::SSE_STATUS_INTERVAL_MS = 500`.

При шторме логов: дроп по soft queue + drop-notice предпочтительнее зависания loop. Логи и status делят одну очередь `/events`.

## Занятость flash и отложенные операции

- `WebServer::isFlashBusy()` блокирует write/delete до безопасной фазы.
- POST JSON → RAM; запись tmp и apply — в `FlashCommitCoordinator::tick()`, чтобы не пересекаться с программой и не трогать LittleFS из async.

## Параллельность: AsyncWebServer и основной `loop`

ESP32-C3 Arduino: AsyncTCP/ESPAsyncWebServer callbacks могут выполняться **вне** `Core::update()` (отдельная задача / ISR-adjacent контекст). Прошивка опирается на одноядерный fair scheduling + явные отложенные точки, а не на мьютексы вокруг всего Core.

### Разрешено из обработчиков async

| Операция | Контекст | Контракт |
|----------|----------|----------|
| `core.otaStreamFeed` / `otaStreamFinish` / `otaStreamAbort` | upload handler `POST /upload` | Пишет в FSM `OtaHandler`; не делает LittleFS commit всего пакета; режим `OTA_UPDATE` **откладывается** через `pendingDeferredOtaFromWebUpload` → drain в `Core::update` |
| `core.onOtaHttpUploadStreamOpenedFromWeb` | upload index==0 | `prepareHttpUploadSession` + флаг deferred mode switch |
| JSON body → RAM | `jsonPostStreamOnBody` | Копия в буфер `CONFIG_JSON_SIZE`; **запись tmp и apply** только из loop (`FlashCommitCoordinator`) |
| `sendJsonBuffered` / RAM UI | GET handlers | Конфиг из кэша; мелкие gzip/схемы из предзагрузки; крупный `app.js.gz` — окно, которое наполняет цикл. LittleFS и `beginResponse(FS)` из колбэка нельзя |
| SSE connect / queue push | `/events` + `tickSseIncremental` | Soft/hard queue caps; tick статуса — из main (`WebServer::update` / Core) |

### Только из основного цикла (`Core::update` / обработчики режимов)

- `ModeManager::switchMode` (в т.ч. вход в `OTA_UPDATE` после deferred OTA flag)
- `FlashCommitCoordinator` apply config/program
- `config.save` / `writeJsonAtomicStream` commit rename
- GSM/MQTT `tick` / cellular suspend
- полный `handleOTAUpdate` progress (кроме feed байт из upload)

### Общие глобалы (web)

- `gUploadCtx` — один активный stream OTA; 409 если уже `active`
- `gOtaHttpUploadAwaitTimedOut` — выставляется из Core timer, читается в upload handler
- `gBootstrapInFlight` — сериализует тяжёлый `/bootstrap` (второй запрос → 409)
- SSE payload/`gSse*` буферы — заполняются из main tick; async только подписывает клиентов

### Logger → SSE

`logger.log` → `sseBroadcastLog` может вызываться из async (upload/API) и из main. Очередь логов soft-gated: при переполнении дроп предпочтительнее зависания. Не полагаться на строгий порядок log vs status frames.

### Стендовый чеклист (ручная регрессия)

1. UI SoftAP, SSE подключён, GSM READY, публикация статуса MQTT.
2. Параллельно: `POST /config/save` (большой JSON) пока идёт программа — должен уйти в deferred, UI видит busy.
3. `POST /upload` stream OTA при активном SSE — cellular suspend после deferred mode switch; feed не блокирует loop на весь файл.
4. Двойной `/bootstrap` / второй `/upload` — 409 Busy.
5. Шторм логов (SERIAL_DEBUG) при SSE — soft queue, без WDT.

Код править при нарушении контракта (flash apply из async, mode switch из upload handler без deferred flag). Текущая реализация соответствует таблице выше.
