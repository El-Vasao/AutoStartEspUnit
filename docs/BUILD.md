# Сборка, прошивка, LittleFS (ESP32-C3)

Основной путь: [`build.py`](../build.py) и [`platformio.ini`](../platformio.ini), окружение `esp32c3`.

```mermaid
flowchart TD
  Py[python build.py release] --> Ver[Version.h]
  Ver --> Pio[pio run -e esp32c3]
  Pio --> Dist[dist/*.bin]
  Dist --> Ota[dist/*-ota.bin]
  Fs[python build.py fs] --> Gz[index.html.gz style.css.gz app.js.gz]
  Gz --> LittleFS[uploadfs]
```

## Требования

- PlatformIO Core
- USB-UART (или USB платы) для загрузки и монитора

## `build.py`

```bash
python build.py --help

python build.py release -u    # прошивка и загрузка
python build.py fs            # UI из трёх gzip → LittleFS uploadfs
python build.py release --no-ota
python build.py -c            # очистка
python build.py -m            # монитор (порт загрузки; журнал — UART1/IO7)
```


Что делает скрипт:

1. Читает semver из [`VERSION`](../VERSION), пишет `include/common/Version.h` (не в git)
2. `pio run -e esp32c3` (и по желанию `--target upload`)
3. Копирует `dist/autostart-<semver>-<type>.bin`
4. Собирает OTA (`dist/*-ota.bin`), если нет `--no-ota`; после успеха двигает `VERSION` на **+0.0.1**
5. `fs`: собирает `index.html.gz`, `style.css.gz` и `app.js.gz` (и схемы), затем `uploadfs`. Если в `data/` есть `config.json`, он попадает в образ LittleFS. OTA-пакет этот файл не включает.

### Версия прошивки

Файл `VERSION` в корне (сейчас `1.0.0`). В бинарник: `FIRMWARE_VERSION_STR` и упакованный `FIRMWARE_VERSION_NUM` (`1.2.3` → `10203`).

| Когда | Как |
|-------|-----|
| Обычная сборка | после успеха **+0.0.1** |
| Новая фича (конфиг ещё читается) | `python build.py release --bump-minor` |
| Ломающий конфиг / протокол / пины | `python build.py release --bump-major` |
| Пересборка той же версии | `--no-bump` |

`patch>99` и `minor>99` перекидываются в следующее поле. Прямой `pio run` не трогает `VERSION` (dev-заголовок из текущего файла).

Прямой PlatformIO тоже работает, если не нужны Version.h, OTA и упаковка gzip:

```bash
python -m platformio run -e esp32c3
python -m platformio run -e esp32c3 -t upload
python -m platformio device monitor
```

## LittleFS

- `board_build.filesystem = littlefs`
- Предпочтительно `python build.py fs`, чтобы дерево UI упаковалось правильно (не заливать сырой `data/` без сборки бандла)

## OTA

Аварийный UI лежит в PROGMEM, если ассетов ФС нет (`FallbackWebPage.h`).

Формат пакета (`OtaHandler`):

1. 4 байта LE — размер прошивки
2. тело прошивки
3. необязательный хвост файлов LittleFS (имя, размер, данные)

См. [`modules/ota.md`](modules/ota.md).

## Заметки

- Разделы: [`partitions.csv`](../partitions.csv)
- Пины и запуск: [`HARDWARE.md`](HARDWARE.md)
- Модуль: XH-C3F (см. [`HARDWARE.md`](HARDWARE.md)); журнал: UART1 TX на IO7 (+ SSE); UART0 только SIM800
