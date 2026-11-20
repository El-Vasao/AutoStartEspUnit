# `ota`: stream-обновление прошивки и ассетов

## Роль
`OtaHandler` принимает OTA-пакет **стримом** из `POST /upload`: чанки идут в FSM → `Update.write` (firmware) → `Update.end` → boot сразу на running → запись UI в LittleFS → `streamFinish` переключает boot на новый слот.

```mermaid
flowchart LR
  Upload[POST /upload] --> Fsm[OtaHandler stream]
  Fsm --> Fw[Update.write firmware]
  Fw --> End[Update.end]
  End --> Pin[boot = running]
  Pin --> Tail[LittleFS UI files]
  Tail --> Slot[boot = new slot]
  Slot --> Reboot[reboot]
```

Основные файлы:
- `include/ota/OtaHandler.h`
- `src/ota/OtaHandler.Core.cpp`, `src/ota/OtaHandler.Stream.cpp`
- upload: `src/web/WebServer.RoutesApi.cpp`

## Формат пакета (без изменений)
1) **Header (4 байта, little-endian)**: размер прошивки `fwSize` (≤ `Ota::APP_IMAGE_MAX` = `0x140000`)
2) **Firmware bytes**: `fwSize` байт → `Update.begin/write`, затем сразу `Update.end(true)`.
   Сессию нельзя держать открытой до конца пакета: LittleFS на той же SPI flash не открывается, пока `Update` не закрыт.
   `begin` берёт слот `esp_ota_get_next_update_partition(NULL)`. На `espressif32` 6.9 это следующий OTA-раздел после **running**, не после текущего boot. `end` делает его загрузочным (`esp_ota_set_boot_partition`). Сразу после успешного `end` слот запоминается через `esp_ota_get_boot_partition()` — это то, что `end` записал в otadata — и boot возвращается на `esp_ota_get_running_partition()`.
3) **Tail (опционально)**: набор файлов для LittleFS:
   - `nameLen` (2 байта LE). `0` или конец потока после fw = конец хвоста
   - `name` (`nameLen` байт). После ведущего `/` путь — из белого списка: gzip shell (`/index.html.gz`, `/style.css.gz`, `/app.js.gz`) или схемы SoftAP (`/ui/*.ui.json`, `/validation/*.schema.json`). `build.py pack_ota` кладёт именно их.
   - `fileSize` (4 байта LE)
   - `fileData` (`fileSize` байт)
   `/config.json`, `/programs/*` и любой другой путь — отказ `failStream` (`путь хвоста не ассет UI`)

Лимит всего пакета: `Ota::FILE_MAX_SIZE` (`0x160000`). Idle без `final`: `Timing::OTA_HTTP_UPLOAD_IDLE_MS` (3 мин).

## Жизненный цикл

1. `POST /upload` (index=0): `prepareHttpUploadSession()` + deferred `switchMode(OTA_UPDATE)`.
2. `enterOTAUpdate`: stop программы, `relay.allOff()`, **SSE не закрывается**; GSM suspend в `handleOTAUpdate`.
3. Чанки → `streamFeed`. По последнему байту прошивки — `Update.end(true)`, запомнить новый слот (`_pendingBoot`), `esp_ota_set_boot_partition(running)`. Дальше файлы хвоста. `_fwCommitted` значит прошивка записана, boot ещё на running.
4. `final` → `streamFinish`. Повторный `Update.end` только если сессия ещё открыта (тот же pin). Когда поток Done и успешен — `esp_ota_set_boot_partition(_pendingBoot)` → ответ `{"success":true,"rebooting":true}` → reboot из `OtaHandler::update`.
5. Обрыв / таймаут / ошибка **до** `Update.end` → `Update.abort()`, выход в NORMAL без перезагрузки при неудачной загрузке.
   Ошибка **после** `Update.end`: `abort` не вызывается (сессия закрыта). Boot уже на running; `failStream` ещё раз вызывает `esp_ota_set_boot_partition(esp_ota_get_running_partition())`.

## Режимы отказа
- Неполный fw до `end` → `Update.abort()`, старый app-слот остаётся boot.
- Хвост после `end` (обрыв, запрещённый путь, ошибка записи) → boot остаётся на текущей прошивке: pin сразу после `end`, плюс повтор в `failStream`. Новый слот при перезагрузке не стартует. `Update.rollBack()` не используется: он зовёт `esp_ota_get_next_update_partition(NULL)`, а этот IDF считает следующий слот от running, поэтому после `end` откат снова выбирает новый слот, а не выполняемый образ.
- Обрыв питания между `esp_ota_set_boot_partition` внутри `Update.end` и нашим pin всё ещё может загрузить новый слот. Это несколько вызовов, не запись хвоста. После pin обрыв на хвосте грузит старую прошивку.
- `streamFinish` при успехе ставит boot на запомненный слот. Если не удалось — `failStream` снова закрепляет running, ребута нет.
- Белый список хвоста: `/index.html.gz`, `/style.css.gz`, `/app.js.gz`, `/ui/settings.ui.json`, `/ui/program-steps.ui.json`, `/validation/settings.schema.json`, `/validation/program.schema.json`, `/validation/programs.index.schema.json`.
- Нехватка места под UI-файл в хвосте → fail на `openWriteStream` (нужен headroom `Ota::STREAM_FS_HEADROOM`).
