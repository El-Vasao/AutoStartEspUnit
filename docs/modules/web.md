# `web`: SoftAP, HTTP API, SSE (UI)

## Роль

Web-подсистема — локальный UI поверх SoftAP/captive portal:

- статические файлы из LittleFS (UI только `*.gz`);
- HTTP API для настроек и программ;
- SSE для статуса и логов.

Основные файлы:

- `include/web/WebServer.h`
- `src/web/WebServer.*.cpp`
- internal: `src/web/internal/*`
- fallback UI: `include/web/FallbackWebPage.h`

## UI sessions

- Клиент периодически обновляет lease (`POST /ui/session`) — для SoftAP idle timeout и log-SSE gating.
- Фиксированный пул сессий без heap; таймаут `WebUi::SESSION_TIMEOUT_MS`, heartbeat — `WebUi::HEARTBEAT_INTERVAL_MS`.
- SSE **не требует** сессию; session — best-effort с FE.

## Контракт по памяти (ESP32-C3)

- Лимиты: `JsonBytes::Web`, `WebSseLimits`, `APConfig` в `include/common/Constants.h`.
- JSON API body: `sendJsonBuffered` — один emit в буфер (~4 KB) + `beginResponse`.
- `String` только кратковременно (параметры AsyncWebServer).

### `/bootstrap`

Один ответ при старте UI: metadata (`version`, `uiLease`, `hwCounts`, `hwMap`, ROMs) **и** `live` snapshot.
Отдельного `/bootstrap/live` нет — дальнейшие обновления идут по SSE.

### SoftAP + cellular

На ESP32-C3 SoftAP UI и GSM/MQTT **сосуществуют**. Cellular suspend при:

- режиме `OTA_UPDATE` (stream flash);
- `SETUP_AP` / `EMERGENCY_AP` (локальная настройка/recovery без модема).

В `NORMAL` и `NORMAL_SILENT` GSM/MQTT обслуживаются одинаково; silent только гасит SoftAP/веб.

Нет `noteHeavyUiTraffic` / `POST /ui/ready` / UI-storm defer / pre-OTA pressure.

Порядок FE:

1. checklist HTTP (`/bootstrap` → schemas → `/config/get` → `/programs`);
2. EventSource `/events` (без искусственных SoftAP gaps на ESP32-C3);
3. best-effort `POST /ui/session` → live updates;
4. на SSE connect — baseline burst (`clocks`→`mode`→`gsm`→`hardware` в одном тике под soft gate);
   unlock после `mode`+`hardware` (или уже из `bootstrap.live`).

## UI visibility contract

1. **Settings subtabs (`.settings-subtab-pane`):** видимость только через CSS
   `display:none` + класс `.active`. **Запрещён** Alpine `x-show` на тех же элементах.
2. **Main tabs (`.tab-pane`):** только Alpine `x-show` по `activeTab`.
3. **System tab:** шелл всегда в DOM после unlock; не гейтить `deviceStatus.loaded`.
4. Смена главного таба — через `uiState.setActiveTab()`.
5. **Вкладки не persist’ятся:** F5 всегда открывает «Панель».
6. **Boot UI:** `#boot-splash` / `html.ui-booting` до `unlock`/`failInit`.

## UI init lifecycle

1. **`#boot-splash`** вне `x-cloak` — виден до `unlock`/`failInit`.
2. **`initLock`** vs **`flashLock`**: `.ui-locked` = `initLock || flashLock`.
3. API: `beginInit()` → progress → `unlock()` / `failInit()`.
4. Head-loader `/app.js`: успех = `alpine:initialized`.
5. Captive `onNotFound`: отсутствующие `.js`/`.css` → **404**, не redirect `/`.
6. SSE panel-ready держит lock до `sseInitDeadlineMs` (12s), иначе degraded; safety ~20s.

## SoftAP UI delivery (index + CSS + app.js)

1. `GET /` → `index.html.gz`
2. `GET /style.css` → `style.css.gz`
3. `GET /app.js` → `app.js.gz`
4. Sentinel `#ui-doc-complete` при неполной доставке HTML — cache-bust reload.

Обязательные UI-ассеты: `WebAssets::REQUIRED` — **на LittleFS только `*.gz`**.

## SSE

- `log` — текст через `logger.log()` (при активной UI-сессии + подписчике);
- incremental status — kinds (`clocks`/`hardware`/…); connect → baseline burst (soft-gated).

Лимиты:

- `WebSseLimits::MAX_SSE_CLIENTS = 4`;
- soft queue `SSE_SOFT_QUEUE_MAX = 8`, hard `SSE_MAX_QUEUED_MESSAGES` (`platformio.ini`);
- период tick: `Timing::SSE_STATUS_INTERVAL_MS = 500`.

При шторме логов: дроп по soft queue предпочтительнее зависания loop.

## Flash busy / deferred

- `WebServer::isFlashBusy()` блокирует write/delete до безопасной фазы.
- POST JSON → tmp; apply deferred с `FlashCommitCoordinator`, чтобы не пересекаться с программой.

## Concurrency: AsyncWebServer vs main `loop`

ESP32-C3 Arduino: AsyncTCP/ESPAsyncWebServer callbacks могут выполняться **вне** `Core::update()` (отдельная задача / ISR-adjacent контекст). Прошивка опирается на одноядерный fair scheduling + явные отложенные точки, а не на мьютексы вокруг всего Core.

### Разрешено из async-handlers

| Операция | Контекст | Контракт |
|----------|----------|----------|
| `core.otaStreamFeed` / `otaStreamFinish` / `otaStreamAbort` | upload handler `POST /upload` | Пишет в FSM `OTAHandler`; не делает LittleFS commit всего пакета; режим `OTA_UPDATE` **откладывается** через `pendingDeferredOtaFromWebUpload` → drain в `Core::update` |
| `core.onOtaHttpUploadStreamOpenedFromWeb` | upload index==0 | `prepareHttpUploadSession` + флаг deferred mode switch |
| JSON body → tmp file | `jsonPostStreamOnBody` | Только stream write в `.tmp`; **apply** только из loop (`FlashCommitCoordinator`) |
| `sendJsonBuffered` / static responses | GET handlers | Краткий `malloc` буфера ответа; не держать flash lock |
| SSE connect / queue push | `/events` + `tickSseIncremental` | Soft/hard queue caps; tick статуса — из main (`WebServer::update` / Core) |

### Только из main loop (`Core::update` / mode handlers)

- `ModeManager::switchMode` (в т.ч. вход в `OTA_UPDATE` после deferred OTA flag)
- `FlashCommitCoordinator` apply config/program
- `config.save` / `writeJsonAtomicStream` commit rename
- GSM/MQTT `tick` / cellular suspend
- полный `handleOTAUpdate` progress (кроме feed байт из upload)

### Shared globals (web)

- `gUploadCtx` — один активный stream OTA; 409 если уже `active`
- `gOtaHttpUploadAwaitTimedOut` — выставляется из Core timer, читается в upload handler
- `gBootstrapInFlight` — сериализует тяжёлый `/bootstrap` (второй запрос → 409)
- SSE payload/`gSse*` буферы — заполняются из main tick; async только подписывает клиентов

### Logger → SSE

`logger.log` → `sseBroadcastLog` может вызываться из async (upload/API) и из main. Очередь логов soft-gated: при переполнении дроп предпочтительнее зависания. Не полагаться на строгий порядок log vs status frames.

### Stress checklist (ручная регрессия)

1. SoftAP UI + SSE connected + GSM READY + MQTT status publish.
2. Параллельно: `POST /config/save` (большой JSON) пока идёт программа — должен уйти в deferred, UI видит busy.
3. `POST /upload` stream OTA при активном SSE — cellular suspend после deferred mode switch; feed не блокирует loop на весь файл.
4. Двойной `/bootstrap` / второй `/upload` — 409 Busy.
5. Шторм логов (SERIAL_DEBUG) при SSE — soft queue, без WDT.

Код править при нарушении контракта (flash apply из async, mode switch из upload handler без deferred flag). Текущая реализация соответствует таблице выше.
