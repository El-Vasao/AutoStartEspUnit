#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AutoStartEspUnit build helper (ESP32-C3).

Wraps PlatformIO: Version.h, firmware build/upload, LittleFS (3 gzip assets), OTA pack.
"""

import os
import sys
import time
import json
import shutil
import subprocess
import struct
import gzip
import tempfile
import urllib.request
import urllib.error
import re
from pathlib import Path

# ====================================================
# КОНФИГУРАЦИЯ
# ====================================================
VERSION = "1.0"
PATHS = {
    # Source code includes "common/Version.h"
    "version_h": "include/common/Version.h",
    "build_info": "dist/build-info.json",
    "dist": "dist",
    "include": "include"
}
PIO_ENVS = {
    "release": "esp32c3",
    "debug": "esp32c3"
}

# ====================================================
# Windows console I/O encoding
# ====================================================
def setup_console_utf8():
    """
    On Windows, default console encodings vary (cp1251/cp866/etc).
    This script prints Cyrillic; enforce UTF-8 where possible to avoid
    mojibake/UnicodeEncodeError.
    """
    if os.name != "nt":
        return
    # Hint Python to prefer UTF-8 for stdio in subprocesses as well.
    os.environ.setdefault("PYTHONIOENCODING", "utf-8")
    try:
        # Python 3.7+: supported in typical installations.
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        # Best-effort: older environments or redirected streams.
        pass

# ====================================================
# УТИЛИТЫ
# ====================================================
def print_step(msg):
    print(f"\n-> {msg}")

def print_done(msg):
    print(f"OK: {msg}")

def print_error(msg):
    print(f"ERR: {msg}")
    sys.exit(1)

def run_cmd(cmd, description=None, live=True):
    """Запуск команды с живым выводом в реальном времени"""
    if description:
        print_step(description)
    
    if live:
        process = subprocess.Popen(cmd, shell=True)
        process.wait()
        if process.returncode != 0:
            print_error(f"Команда завершилась с ошибкой: {cmd}")
        return process
    else:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        if result.returncode != 0:
            print_error(f"{cmd}\n{result.stderr}")
        return result

def run_cmd_capture(cmd: str) -> str:
    """Run command and return stdout text (raises on non-zero)."""
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        print_error(f"{cmd}\n{result.stderr}")
    return result.stdout

# ====================================================
# PlatformIO command helper
# ====================================================
def pio_cmd(args: str) -> str:
    """
    Returns a shell command to run PlatformIO.
    Uses `pio` if available, otherwise falls back to `python -m platformio`.
    """
    exe = shutil.which("pio")
    if not exe and os.name == "nt":
        # Typical PlatformIO venv location on Windows.
        cand = Path.home() / ".platformio" / "penv" / "Scripts" / "pio.exe"
        if cand.exists():
            exe = str(cand)
    if exe:
        return f"\"{exe}\" {args}"
    # Fallback: try Python module (may be absent depending on installation).
    return f"\"{sys.executable}\" -m platformio {args}"

# ====================================================
# OTA PACK (firmware + LittleFS assets)
# ====================================================
GZIP_EXTENSIONS = {".js", ".css", ".html", ".ico"}

def cleanup_gz_artifacts(files_dir: Path):
    """
    Удаляет *.gz в исходной папке data/.
    Эти файлы создаются как артефакты сборки/выгрузки FS и не должны «залипать» в git.
    """
    try:
        if not files_dir.exists():
            return
        removed = 0
        for p in files_dir.rglob("*.gz"):
            try:
                p.unlink()
                removed += 1
            except OSError:
                # best-effort cleanup
                pass
        if removed:
            print_done(f"Удалены артефакты gzip в {files_dir}: {removed} файл(ов)")
    except Exception:
        # best-effort cleanup
        pass

JS_LIBS = [
    # Жесты (double-tap, long-press) для мобильного UI
    "js/hammer.min.js",
    # Официальные плагины Alpine (небольшие, но сильно упрощают UI):
    # - persist: проще работать с localStorage и восстановлением состояния
    # - focus: доступность и нормальное управление фокусом в диалогах/модалках
    "js/alpine.persist.min.js",
    "js/alpine.focus.min.js",
    # - collapse: аккордеоны/раскрывающиеся секции (удобно для настроек)
    # - intersect: дешёвая “ленивая” инициализация тяжёлых частей (логи/списки программ)
    "js/alpine.collapse.min.js",
    "js/alpine.intersect.min.js",
    # - morph: аккуратный DOM-morph для частичных обновлений (на будущее)
    # - anchor: позиционирование тултипов/поповеров/меню без ручной математики
    # - sort: drag-and-drop сортировка (можно заменить кнопки вверх/вниз в редакторе программ)
    "js/alpine.morph.min.js",
    "js/alpine.anchor.min.js",
    "js/alpine.sort.min.js",
    # Валидатор JSON Schema (Draft 2020-12, browser bundle)
    "js/ajv2020.min.js",
    # Важно: Alpine.start откладываем до регистрации stores/components
    "js/app/alpine-defer.js",
    # Важно: Alpine core должен идти ПОСЛЕ плагинов (в CDN-подобной загрузке)
    "js/alpine.min.js",
    # Модули приложения (без бандлера; склеиваем строго в этом порядке)
    "js/app/ns.js",
    "js/app/contract.js",
    "js/app/ui-maps.js",
    "js/app/utils.js",
    "js/app/dom-ids.js",
    "js/app/field-shell.js",
    "js/app/validation-utils.js",
    "js/app/schema-normalize.js",
    "js/app/coerce.js",
    "js/app/options.js",
    "js/app/api.js",
    "js/app/overlay.js",
    "js/app/program-editor.js",
    "js/app/stores/settings-ui-schema-store.js",
    "js/app/stores/settings-validator-store.js",
    "js/app/stores/program-validator-store.js",
    "js/app/stores/settings-form-ui-store.js",
    "js/app/stores/program-form-ui-store.js",
    "js/app/stores/ui-toast-store.js",
    "js/app/stores/ui-busy-modal-store.js",
    "js/app/stores/ui-statusbar-compat-store.js",
    "js/app/stores/ui-formatters-store.js",
    "js/app/stores/ui-notification-compat-store.js",
    "js/app/stores/ui-state-store.js",
    "js/app/stores/ui-confirm-dialog-store.js",
    "js/app/stores/program-steps-ui-schema-store.js",
    "js/app/stores/device-status-store.js",
    "js/app/stores/ui-log-console-store.js",
    "js/app/stores/programs-api-store.js",
    "js/app/stores/settings-store.js",
    "js/app/stores/device-ota-update-store.js",
    "js/app/stores/device-runtime-controls-store.js",
    "js/app/stores/device-maintenance-actions-store.js",
    "js/app/stores/stores-init.js",
    "js/app/gestures.js",
    "js/app/sse.js",
    "js/app/core.js",
    # Запускаем Alpine после регистрации приложения
    "js/app/alpine-start.js",
]

PRELOADED_JSON_FILES = [
    ("programStepsUiSchema", "ui/program-steps.ui.json"),
    ("settingsUiSchema", "ui/settings.ui.json"),
    ("settingsValidationSchema", "validation/settings.schema.json"),
    ("programValidationSchema", "validation/program.schema.json"),
    ("programIndexValidationSchema", "validation/programs.index.schema.json"),
]

LIB_URLS = {
    # Пинним версии, чтобы сборка была воспроизводимой.
    "js/hammer.min.js": "https://cdn.jsdelivr.net/npm/@egjs/hammerjs@2.0.17/dist/hammer.min.js",
    "js/alpine.min.js": "https://cdn.jsdelivr.net/npm/alpinejs@3.14.8/dist/cdn.min.js",
    "js/alpine.persist.min.js": "https://cdn.jsdelivr.net/npm/@alpinejs/persist@3.15.8/dist/cdn.min.js",
    "js/alpine.focus.min.js": "https://cdn.jsdelivr.net/npm/@alpinejs/focus@3.15.8/dist/cdn.min.js",
    "js/alpine.collapse.min.js": "https://cdn.jsdelivr.net/npm/@alpinejs/collapse@3.15.8/dist/cdn.min.js",
    "js/alpine.intersect.min.js": "https://cdn.jsdelivr.net/npm/@alpinejs/intersect@3.15.8/dist/cdn.min.js",
    "js/alpine.morph.min.js": "https://cdn.jsdelivr.net/npm/@alpinejs/morph@3.15.8/dist/cdn.min.js",
    "js/alpine.anchor.min.js": "https://cdn.jsdelivr.net/npm/@alpinejs/anchor@3.15.8/dist/cdn.min.js",
    "js/alpine.sort.min.js": "https://cdn.jsdelivr.net/npm/@alpinejs/sort@3.15.8/dist/cdn.min.js",
    # Ajv (browser bundle for JSON Schema draft 2020-12)
    "js/ajv2020.min.js": "https://cdn.jsdelivr.net/npm/ajv-dist@8.17.1/dist/ajv2020.min.js",
}

def fetch_to_file(url: str, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    try:
        with urllib.request.urlopen(url, timeout=30) as r:
            data = r.read()
        if not data or len(data) < 64:
            raise RuntimeError(f"Downloaded file is too small ({len(data)} bytes)")
        dst.write_bytes(data)
    except (urllib.error.URLError, urllib.error.HTTPError) as e:
        raise RuntimeError(f"Failed to download {url}: {e}") from e

def ensure_js_libs(files_dir: Path, cache_dir: Path):
    """
    Гарантируем наличие js библиотек для bundling.
    Порядок:
    - если файл уже есть в data/js — используем его
    - иначе пытаемся взять из cache
    - иначе скачиваем по URL и кладём в cache
    """
    for rel in JS_LIBS:
        target = files_dir / rel
        if target.exists():
            continue

        # Only libs listed in LIB_URLS are downloadable. Everything else in JS_LIBS
        # is expected to be part of the project (data/). Provide a clear error.
        url = LIB_URLS.get(rel)
        if not url:
            raise RuntimeError(
                "Missing local JS file required for bundling:\n"
                f"  rel: {rel}\n"
                f"  expected at: {target}\n"
                "Fix: restore/create this file under data/, or remove it from JS_LIBS."
            )

        cached = cache_dir / rel
        if cached.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(cached, target)
            print(f"  LIB: {rel} <- cache")
            continue

        print(f"  LIB: downloading {rel}")
        fetch_to_file(url, cached)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(cached, target)
        print(f"  LIB: {rel} downloaded")

def should_gzip(rel_path: str) -> bool:
    return Path(rel_path).suffix.lower() in GZIP_EXTENSIONS

def gzip_file(src_path: Path, dst_path: Path):
    dst_path.parent.mkdir(parents=True, exist_ok=True)
    with src_path.open("rb") as f_in, gzip.open(dst_path, "wb", compresslevel=9) as f_out:
        shutil.copyfileobj(f_in, f_out)

def copy_index_html(files_dir: Path, out_path: Path) -> None:
    """Copy data/index.html as-is (CSS is a separate /style.css asset)."""
    tpl_path = files_dir / "index.html"
    if not tpl_path.is_file():
        raise RuntimeError(f"Missing {tpl_path}")
    if "ui-doc-complete" not in tpl_path.read_text(encoding="utf-8"):
        raise RuntimeError("data/index.html must contain #ui-doc-complete delivery sentinel")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(tpl_path, out_path)

def build_script_bundle(files_dir: Path) -> bytes:
    """
    bundle = data/script.js + (optional) downloaded/local libs.
    """
    parts = []
    preload_bytes = build_preloaded_schemas_js(files_dir)
    # Важно: скрипты, которые подписываются на `alpine:init`, должны быть
    # загружены ДО Alpine core, иначе они могут “проспать” событие.
    for rel in JS_LIBS:
        p = files_dir / rel
        if p.exists():
            parts.append(b"\n;\n")
            parts.append(p.read_bytes())
            if rel == "js/app/ns.js" and preload_bytes:
                parts.append(b"\n;\n")
                parts.append(preload_bytes)

    base = files_dir / "script.js"
    if base.exists():
        parts.append(b"\n;\n")
        parts.append(base.read_bytes())
    return b"".join(parts)

def build_preloaded_schemas_js(files_dir: Path) -> bytes:
    """
    Генерирует JS, который подмешивает статические JSON-схемы в window.APP.preloaded.
    JSON-файлы остаются в data/ и на LittleFS, но UI использует только bundled-версию.
    """
    payload = {}

    # Аппаратные лимиты — “истина” на стороне прошивки: Pin::INPUT_PINS/RELAY_PINS/MAX_SENSORS.
    # Подмешиваем их в валидационную схему, чтобы UI соответствовал реальной сборке устройства.
    hw = read_hw_limits_from_pins(find_pins_h())
    for key, rel_path in PRELOADED_JSON_FILES:
        src = files_dir / rel_path
        if not src.exists():
            raise RuntimeError(f"Missing preload source: {src}")
        with src.open("r", encoding="utf-8") as f:
            obj = json.load(f)
            if rel_path.replace("\\", "/") == "validation/settings.schema.json":
                obj = patch_settings_schema_with_hw_limits(obj, hw)
            payload[key] = obj

    js = (
        "(function(){\n"
        "  window.APP = window.APP || {};\n"
        "  window.APP.preloaded = window.APP.preloaded || {};\n"
        f"  Object.assign(window.APP.preloaded, {json.dumps(payload, ensure_ascii=False, separators=(',', ':'))});\n"
        "})();\n"
    )
    return js.encode("utf-8")

def find_pins_h() -> Path | None:
    """Return include/common/Pins.h if present."""
    p = Path(__file__).resolve().parent / "include" / "common" / "Pins.h"
    return p if p.exists() else None


def read_hw_limits_from_pins(pins_h: Path | None) -> dict:
    """
    Достаёт аппаратные лимиты из include/Pins.h (namespace Pin).
    Возвращает { relays: int, inputs: int, sensors: int }.
    """
    if not pins_h or not pins_h.exists():
        # Best-effort: allow FS build even if header layout differs.
        # Limits will remain unpatched in JSON schema in that case.
        print("WARN: Pins.h not found; skip hw limits patching")
        return {"inputs": 0, "relays": 0, "sensors": 0}
    txt = pins_h.read_text(encoding="utf-8", errors="ignore")

    def count_array(name: str) -> int:
        # Ищем: constexpr uint8_t NAME[] = { ... };
        m = re.search(rf"{re.escape(name)}\s*\[\s*\]\s*=\s*\{{([\s\S]*?)\}}\s*;", txt, flags=re.MULTILINE)
        if not m:
            raise RuntimeError(f"Cannot parse {name}[] from Pins.h")
        body = m.group(1)
        # Вычищаем C++ комментарии, чтобы корректно посчитать элементы массива.
        body = re.sub(r"//.*?$", "", body, flags=re.MULTILINE)
        body = re.sub(r"/\*[\s\S]*?\*/", "", body, flags=re.MULTILINE)
        # Делим по запятым и убираем пустые элементы.
        items = [x.strip() for x in body.split(",")]
        items = [x for x in items if x]
        return len(items)

    def read_const_u8(name: str) -> int:
        m = re.search(rf"{re.escape(name)}\s*=\s*([0-9]+)\s*;", txt)
        if not m:
            raise RuntimeError(f"Cannot parse {name} from Pins.h")
        return int(m.group(1), 10)

    return {
        "inputs": count_array("INPUT_PINS"),
        "relays": count_array("RELAY_PINS"),
        "sensors": read_const_u8("MAX_SENSORS"),
    }

def patch_settings_schema_with_hw_limits(schema: dict, hw: dict) -> dict:
    """
    Подгоняет JSON-схему под аппаратные лимиты.
    Сама схема остаётся “истиной” для UI/прошивки, а числовые границы берём из namespace Pin.
    """
    sc = json.loads(json.dumps(schema))  # deep clone
    inputs = max(0, int(hw.get("inputs", 0)))
    relays = max(0, int(hw.get("relays", 0)))
    sensors = max(0, int(hw.get("sensors", 0)))

    def set_max(node, v):
        if isinstance(node, dict):
            node["maximum"] = int(v)

    # vehicle.starter_relay_id uses ids (no hwCount-based limits)

    # Размеры массивов
    try:
        sc["properties"]["sensors"]["maxItems"] = sensors
    except Exception:
        pass
    try:
        sc["properties"]["inputs"]["maxItems"] = inputs
    except Exception:
        pass

    return sc

def build_fs_tree(files_dir: Path, gzip_enabled: bool = True, include_config_json: bool = False) -> Path:
    """
    Готовим виртуальное дерево LittleFS из data/:
    - bundling (libs + script.js) -> app.js(.gz)
    - index.html + style.css copied as-is (.gz) — 3-gzip SoftAP delivery
    - gzip выбранных расширений
    - по умолчанию исключаем config.json из обхода (чтобы не дублировать), затем:
      - include_config_json=False: в образ не кладём (OTA-пакет — без пользовательского конфига)
      - include_config_json=True: копируем data/config.json в корень образа (заливка FS на устройство)

    Возвращает Path на временную директорию (её должен удалить вызывающий код).
    """
    cache_dir = Path(".cache") / "web-libs"
    temp_dir = Path(tempfile.mkdtemp(prefix="fs_tree_"))

    if gzip_enabled:
        ensure_js_libs(files_dir, cache_dir)
        bundle = build_script_bundle(files_dir)
        if not bundle:
            shutil.rmtree(temp_dir, ignore_errors=True)
            raise RuntimeError("Пустой JS bundle — проверьте data/script.js и js/libs (build_script_bundle).")
        copy_index_html(files_dir, temp_dir / "index.html")
        css_src = files_dir / "style.css"
        if not css_src.is_file():
            shutil.rmtree(temp_dir, ignore_errors=True)
            raise RuntimeError(f"Missing {css_src}")
        shutil.copy2(css_src, temp_dir / "style.css")
        (temp_dir / "app.js").write_bytes(bundle)

    for root, dirs, files in os.walk(files_dir):
        for file in files:
            full_path = Path(root) / file
            rel_path = os.path.relpath(full_path, files_dir).replace("\\", "/")

            if rel_path == "config.json":
                continue

            # В gzip-режиме: сырой шелл/бандл не кладём — собираются выше.
            if gzip_enabled:
                if rel_path in JS_LIBS:
                    continue
                if rel_path == "script.js":
                    continue
                if rel_path == "style.css":
                    continue
                if rel_path == "index.html":
                    continue

            target_path = temp_dir / rel_path
            target_path.parent.mkdir(parents=True, exist_ok=True)

            # Подгоняем схему валидации под аппаратные лимиты (namespace Pin).
            if rel_path == "validation/settings.schema.json":
                hw = read_hw_limits_from_pins(find_pins_h())
                obj = json.loads(full_path.read_text(encoding="utf-8"))
                obj = patch_settings_schema_with_hw_limits(obj, hw)
                target_path.write_text(json.dumps(obj, ensure_ascii=False, indent=2), encoding="utf-8")
            else:
                shutil.copy2(full_path, target_path)

    # gzip-уем выбранные файлы и удаляем оригиналы (чтобы на FS лежали только *.gz)
    if gzip_enabled:
        for root, dirs, files in os.walk(temp_dir):
            for file in list(files):
                p = Path(root) / file
                rel = os.path.relpath(p, temp_dir).replace("\\", "/")
                if should_gzip(rel) and not rel.endswith(".gz"):
                    gz = p.with_name(p.name + ".gz")
                    gzip_file(p, gz)
                    p.unlink()

    if include_config_json:
        cfg_src = files_dir / "config.json"
        if cfg_src.is_file():
            shutil.copy2(cfg_src, temp_dir / "config.json")
        else:
            print(f"WARN: Нет {cfg_src} — на устройство конфиг не попадёт (ожидался include_config_json).")

    return temp_dir

def pack_ota(firmware_path: str, files_dir: str, output_path: str, gzip_enabled: bool = True):
    firmware_path = Path(firmware_path)
    files_dir = Path(files_dir)
    output_path = Path(output_path)

    firmware = firmware_path.read_bytes()
    fw_size = len(firmware)

    # Подготовка виртуального дерева FS (OTA: без config.json намеренно)
    temp_dir = build_fs_tree(files_dir, gzip_enabled=gzip_enabled, include_config_json=False)
    try:
        # Пакуем OTA
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("wb") as out:
            out.write(struct.pack("<I", fw_size))
            out.write(firmware)

            for root, dirs, files in os.walk(temp_dir):
                for file in files:
                    full_path = Path(root) / file
                    rel_path = os.path.relpath(full_path, temp_dir).replace("\\", "/")
                    content = full_path.read_bytes()
                    name_bytes = rel_path.encode("utf-8")
                    out.write(struct.pack("<H", len(name_bytes)))
                    out.write(name_bytes)
                    out.write(struct.pack("<I", len(content)))
                    out.write(content)
        print_done(f"OTA-файл создан: {output_path} ({output_path.stat().st_size//1024} KB)")
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)

# ====================================================
# FS UPLOAD (LittleFS)
# ====================================================
def upload_filesystem(files_dir: str = "data", gzip_enabled: bool = True, cleanup_gz: bool = True):
    """
    Заливка LittleFS на контроллер:
    - готовим временное дерево FS (index.html.gz + style.css.gz + app.js.gz + schemas)
    - если есть data/config.json — кладём его в образ (раньше скрипт его намеренно выкидывал)
    - ВРЕМЕННО подменяем папку data/ в проекте
    - вызываем `pio run --target uploadfs`
    - восстанавливаем исходную data/ даже при ошибке

    Почему так: это самый совместимый путь без завязки на версии PlatformIO CLI.
    """
    
    files_dir = Path(files_dir)
    if not files_dir.exists():
        print_error(f"Папка '{files_dir}' не найдена")

    print(f"\n=== ЗАЛИВКА FS ({files_dir}) ===")
    temp_fs = build_fs_tree(files_dir, gzip_enabled=gzip_enabled, include_config_json=True)
    backup_dir = Path(tempfile.mkdtemp(prefix="data_backup_"))
    try:
        # backup исходной data/
        if files_dir.exists():
            shutil.copytree(files_dir, backup_dir / "data", dirs_exist_ok=True)

        # подмена data/ на подготовленное дерево
        if files_dir.exists():
            shutil.rmtree(files_dir, ignore_errors=True)
        shutil.copytree(temp_fs, files_dir, dirs_exist_ok=True)

        env_name = PIO_ENVS["release"]
        run_cmd(pio_cmd(f"run -e {env_name} --target uploadfs"), "PlatformIO uploadfs (LittleFS)")

        print_done("Файловая система залита")
    finally:
        # восстановление исходной data/
        try:
            if files_dir.exists():
                shutil.rmtree(files_dir, ignore_errors=True)
            src_backup = backup_dir / "data"
            if src_backup.exists():
                shutil.copytree(src_backup, files_dir, dirs_exist_ok=True)
        finally:
            shutil.rmtree(temp_fs, ignore_errors=True)
            shutil.rmtree(backup_dir, ignore_errors=True)
            # На всякий случай чистим *.gz, если они попали в исходный data/ (часто остаются после прошлых билдов)
            if cleanup_gz:
                cleanup_gz_artifacts(files_dir)

# ====================================================
# СПРАВКА
# ====================================================
def show_help():
    print(f"""
AutoStartEspUnit builder v{VERSION} (ESP32-C3)

USAGE:
  python build.py [COMMAND] [OPTIONS]

COMMANDS:
  release             Build firmware (SSE/web logs only; no SERIAL_DEBUG)
  debug               Build with SERIAL_DEBUG (UART shared with GSM — legacy)
  fs                  Build 3-gzip UI tree and upload LittleFS (uploadfs)

OPTIONS:
  -u, --upload        Upload firmware after build
  -m, --monitor       Serial monitor
  -c, --clean         Clean PlatformIO + generated Version.h / build-info
  -i, --info          Tool versions
  --keep-gz           Keep *.gz leftovers under data/
  --no-ota            Skip OTA package in dist/
  -h, --help          This help

EXAMPLES:
  python build.py release -u
  python build.py debug -u
  python build.py fs
  python build.py release --no-ota
""")

# ====================================================
# ГЕНЕРАЦИЯ ВЕРСИИ
# ====================================================
class VersionGen:
    def __init__(self):
        self.timestamp = int(time.time())
        self.version = str(self.timestamp)

    def save(self, build_type="release"):
        if build_type not in ("release", "debug"):
            build_type = "release"
        os.makedirs(PATHS["include"], exist_ok=True)
        os.makedirs(os.path.dirname(PATHS["version_h"]), exist_ok=True)

        serial_macro = ""
        if build_type == "debug":
            serial_macro = "#define SERIAL_DEBUG 1\n"

        content = f'''// Auto-generated
#pragma once
{serial_macro}#define FIRMWARE_VERSION_NUM {self.timestamp}ULL
#define FIRMWARE_VERSION_STR "{self.version}"
#define FIRMWARE_BUILD_TYPE "{build_type}"
'''

        with open(PATHS["version_h"], "w") as f:
            f.write(content)

        os.makedirs(PATHS["dist"], exist_ok=True)
        meta = {
            "version": self.timestamp,
            "version_str": self.version,
            "build_type": build_type,
            "suffix": build_type,
        }
        with open(PATHS["build_info"], "w") as f:
            json.dump(meta, f)

        print(f"  Версия: {self.version} ({build_type})")

# ====================================================
# ПРОВЕРКА ПИНОВ
# ====================================================
def check_pins():
    """Print C3 pad remap used by this firmware (same PCB as ESP-12)."""
    pins = {
        "GSM_TX": 21,
        "GSM_RX": 20,
        "ONEWIRE": 9,
        "BUTTON": 10,
        "VBAT_ADC": 1,
    }
    for name, pin in pins.items():
        print(f"  {name}: GPIO{pin}")
    print("  RELAY1..5: GPIO 3,19,18,8,2")
    print("  UART0 shared: SIM800 + debug Serial (legacy)")

# ====================================================
# СБОРКА
# ====================================================
def build(upload=False, skip_ota=False, build_type="release"):
    if build_type not in ("release", "debug"):
        build_type = "release"
    print(f"\n=== BUILD {build_type.upper()} ===")

    VersionGen().save(build_type)

    print_step("Pins (ESP32-C3)")
    check_pins()

    os.environ["BUILD_TYPE"] = build_type
    env_name = PIO_ENVS.get(build_type, PIO_ENVS["release"])

    run_cmd(pio_cmd(f"run -e {env_name}"), f"PlatformIO build ({env_name})")

    src = f".pio/build/{env_name}/firmware.bin"
    if not os.path.exists(src):
        print_error("firmware.bin not found")

    with open(PATHS["build_info"]) as f:
        ver = json.load(f)["version"]

    name = f"autostart-{ver}-{build_type}.bin"
    dst = os.path.join(PATHS["dist"], name)
    shutil.copy2(src, dst)
    size = os.path.getsize(dst)

    print_done(f"Binary: {name} ({size // 1024} KB)")

    if not skip_ota:
        ota_name = f"autostart-{ver}-{build_type}-ota.bin"
        ota_path = os.path.join(PATHS["dist"], ota_name)
        files_dir = "data"
        if os.path.exists(files_dir):
            print_step("Packing OTA")
            pack_ota(dst, files_dir, ota_path, gzip_enabled=True)
        else:
            print("  data/ missing — OTA pack skipped")

    if upload:
        run_cmd(pio_cmd(f"run -e {env_name} --target upload"), "Upload")
        print_done("Upload done")

# ====================================================
# КОМАНДЫ
# ====================================================
def clean():
    print("\n=== ОЧИСТКА ===")
    run_cmd(pio_cmd("run --target clean"), "PlatformIO clean", live=False)
    legacy_version = Path("include") / "Version.h"
    for f in [PATHS["version_h"], PATHS["build_info"], str(legacy_version)]:
        if os.path.exists(f):
            os.remove(f)
            print(f"  Удалено: {f}")
    print_done("Очистка завершена")

def monitor():
    print("\n=== МОНИТОР ПОРТА ===")
    run_cmd(pio_cmd("device monitor"), "Монитор", live=True)

def info():
    print(f"\nAutoStart Builder v{VERSION}")
    print(f"Python: {sys.version.split()[0]}")
    ver = run_cmd(pio_cmd("--version"), live=False)
    print(f"PlatformIO: {ver.stdout.strip()}")
    print()

# ====================================================
# MAIN
# ====================================================
def main():
    setup_console_utf8()
    args = sys.argv[1:]
    keep_gz = "--keep-gz" in args
    
    if not args:
        show_help()
        return
    
    if "-h" in args or "--help" in args:
        show_help()
        return
    
    if "-c" in args or "--clean" in args:
        clean()
        return
    
    if "-i" in args or "--info" in args:
        info()
        return
    
    if "-m" in args or "--monitor" in args:
        monitor()
        return

    if "fs" in args or "uploadfs" in args:
        upload_filesystem("data", gzip_enabled=True, cleanup_gz=(not keep_gz))
        return

    build_type = "debug" if "debug" in args else "release"
    upload = "-u" in args or "--upload" in args
    skip_ota = "--no-ota" in args

    build(upload, skip_ota, build_type)
    if not keep_gz:
        cleanup_gz_artifacts(Path("data"))

if __name__ == "__main__":
    main()