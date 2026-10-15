# PlatformIO pre-script: create include/common/Version.h when it is missing.
#
# build.py regenerates Version.h for every release/debug build. A plain
# `pio run` / `pio test` (CI, agents, IDE) has no build.py step, so this script
# writes a "dev" header with the same macros. An existing file is never touched.
Import("env")  # type: ignore  # PlatformIO SCons

import os
import time

_HEADER = """// Auto-generated (scripts/ensure_version_h.py, dev build)
#pragma once
#define FIRMWARE_VERSION_NUM {ts}ULL
#define FIRMWARE_VERSION_STR "{ts}"
#define FIRMWARE_BUILD_TYPE "dev"
"""


def _ensure_version_h() -> None:
    project_dir = env.subst("$PROJECT_DIR")  # type: ignore  # noqa: F821
    path = os.path.join(project_dir, "include", "common", "Version.h")
    if os.path.isfile(path):
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(_HEADER.format(ts=int(time.time())))
    print("ensure_version_h: created " + path)


_ensure_version_h()
