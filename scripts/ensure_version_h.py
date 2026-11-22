# PlatformIO pre-script: create include/common/Version.h when it is missing.
#
# build.py regenerates Version.h for every release/debug build and advances VERSION.
# A plain `pio run` / `pio test` (CI, agents, IDE) has no build.py step, so this
# script writes a "dev" header from the VERSION file without bumping it.
# An existing Version.h is never touched.
#
# Note: SCons execs this file without defining __file__ — use $PROJECT_DIR only.
Import("env")  # type: ignore  # PlatformIO SCons

import os
import sys

_HEADER = """// Сгенерировано автоматически (scripts/ensure_version_h.py, сборка dev)
#pragma once
#define FIRMWARE_VERSION_NUM {num}UL
#define FIRMWARE_VERSION_STR "{ver}"
#define FIRMWARE_BUILD_TYPE "dev"
"""


def _ensure_version_h() -> None:
    project_dir = env.subst("$PROJECT_DIR")  # type: ignore  # noqa: F821
    path = os.path.join(project_dir, "include", "common", "Version.h")
    if os.path.isfile(path):
        return

    scripts_dir = os.path.join(project_dir, "scripts")
    if scripts_dir not in sys.path:
        sys.path.insert(0, scripts_dir)
    import firmware_version as fwver  # noqa: E402

    os.makedirs(os.path.dirname(path), exist_ok=True)
    major, minor, patch = fwver.read()
    ver = fwver.format_ver(major, minor, patch)
    num = fwver.to_num(major, minor, patch)
    with open(path, "w", encoding="utf-8") as f:
        f.write(_HEADER.format(num=num, ver=ver))
    print("ensure_version_h: created " + path + f" ({ver})")


_ensure_version_h()
