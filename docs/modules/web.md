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

На ESP32-C3 SoftAP UI и GSM/MQTT **сосуществуют**. Cellular suspend только при:

- OTA upload pressure;
- SoftAP down в NORMAL (модем тихий, пока AP снова не поднят).

Нет `noteHeavyUiTraffic` / `POST /ui/ready` / UI-storm defer.

Порядок FE:

1. checklist HTTP (`/bootstrap` → schemas → `/config/get` → `/programs`);
2. короткий `sseStartDelayMs`;
3. best-effort `POST /ui/session` → EventSource `/events`;
4. на SSE connect — paced baseline; unlock после `mode`+`hardware` (или уже из `bootstrap.live`).

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
- incremental status — kinds (`clocks`/`hardware`/…); connect → paced baseline.

Лимиты:

- `WebSseLimits::MAX_SSE_CLIENTS = 4`;
- soft queue `SSE_SOFT_QUEUE_MAX = 8`, hard `SSE_MAX_QUEUED_MESSAGES` (`platformio.ini`);
- период tick: `Timing::SSE_STATUS_INTERVAL_MS = 1000`.

При шторме логов: дроп по soft queue предпочтительнее WDT.

## Flash busy / deferred

- `WebServer::isFlashBusy()` блокирует write/delete до безопасной фазы.
- POST JSON → tmp; apply deferred с `FlashCommitCoordinator`, чтобы не пересекаться с программой.
