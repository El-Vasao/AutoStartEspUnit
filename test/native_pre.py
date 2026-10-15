# Host [env:native]: WinLibs/MinGW embeds absolute install paths. When the
# username contains a space, ld splits default-manifest.o and link fails.
# Point GCC_EXEC_PREFIX at the 8.3 short path so search dirs stay space-free.
Import("env")  # type: ignore  # PlatformIO SCons

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
        # Still try: install prefix may contain spaces even if PATH entry is short.
        pass

    gxx_abs = os.path.abspath(gxx) if gxx else ""
    if not gxx_abs or not os.path.isfile(gxx_abs):
        return

    # .../mingw64/bin/g++.exe → .../mingw64/lib/gcc/
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
