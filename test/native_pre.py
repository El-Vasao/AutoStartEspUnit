# Хост [env:native]: WinLibs/MinGW вшивает абсолютные пути установки. Если в
# имени пользователя есть пробел, ld режет default-manifest.o и линковка падает.
# GCC_EXEC_PREFIX указывает на короткий путь 8.3, чтобы каталоги поиска были без пробелов.
Import("env")  # type: ignore  # SCons PlatformIO

import os
import shutil


def _win_short_path(path: str) -> str:
    if os.name != "nt":
        return path
    try:
        import ctypes
        from ctypes import wintypes

        GetShortPathNameW = ctypes.windll.kernel32.GetShortPathNameW
        GetShortPathNameW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD]
        GetShortPathNameW.restype = wintypes.DWORD
        buf = ctypes.create_unicode_buffer(260)
        n = GetShortPathNameW(path, buf, len(buf))
        if n and n < len(buf):
            return buf.value
    except Exception:
        pass
    return path


def _configure_gcc_exec_prefix() -> None:
    gxx = env.subst("$CXX") if env.get("CXX") else ""
    if not gxx or gxx in ("g++", "c++"):
        gxx = shutil.which("g++") or ""
    if not gxx or " " not in os.path.abspath(gxx):
        # Всё равно пробуем: префикс установки может содержать пробелы, даже если запись в PATH короткая.
        pass

    gxx_abs = os.path.abspath(gxx) if gxx else ""
    if not gxx_abs or not os.path.isfile(gxx_abs):
        return

    # .../mingw64/bin/g++.exe → каталог .../mingw64/lib/gcc/
    mingw_root = os.path.dirname(os.path.dirname(gxx_abs))
    lib_gcc = os.path.join(mingw_root, "lib", "gcc")
    if not os.path.isdir(lib_gcc):
        return

    short_lib_gcc = _win_short_path(lib_gcc)
    if " " in short_lib_gcc:
        print("native_pre: warning: short path still has spaces: " + short_lib_gcc)
        return

    prefix = short_lib_gcc
    if not prefix.endswith(("\\", "/")):
        prefix += os.sep

    env["ENV"]["GCC_EXEC_PREFIX"] = prefix
    os.environ["GCC_EXEC_PREFIX"] = prefix
    print("native_pre: GCC_EXEC_PREFIX=" + prefix)


_configure_gcc_exec_prefix()
