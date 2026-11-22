# Shared firmware semver helpers for build.py and ensure_version_h.py.
#
# Scheme (file VERSION at repo root):
# - patch (+0.0.1): every successful `build.py release|debug` (default)
# - minor (+0.1.0): `--bump-minor` — new feature / SoftAP·MQTT surface, config still loads
# - major (+1.0.0): `--bump-major` — breaking config, protocol, or pin map
# Auto-roll: patch>99 → minor+1; minor>99 → major+1
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION_PATH = ROOT / "VERSION"


def parse(text: str) -> tuple[int, int, int]:
    s = (text or "").strip().splitlines()[0].strip()
    parts = s.split(".")
    if len(parts) != 3:
        raise ValueError(f"VERSION must be MAJOR.MINOR.PATCH, got {text!r}")
    major, minor, patch = (int(parts[0]), int(parts[1]), int(parts[2]))
    if major < 0 or minor < 0 or patch < 0:
        raise ValueError(f"VERSION components must be >= 0, got {text!r}")
    return major, minor, patch


def format_ver(major: int, minor: int, patch: int) -> str:
    return f"{major}.{minor}.{patch}"


def to_num(major: int, minor: int, patch: int) -> int:
    """Packed decimal: 1.2.3 → 10203 (fits uint32; human-readable)."""
    if minor > 99 or patch > 99:
        raise ValueError(f"minor/patch must be 0..99 for packing, got {major}.{minor}.{patch}")
    return major * 10000 + minor * 100 + patch


def bump_patch(major: int, minor: int, patch: int) -> tuple[int, int, int]:
    patch += 1
    if patch > 99:
        patch = 0
        minor += 1
    if minor > 99:
        minor = 0
        major += 1
    return major, minor, patch


def bump_minor(major: int, minor: int, _patch: int) -> tuple[int, int, int]:
    minor += 1
    if minor > 99:
        minor = 0
        major += 1
    return major, minor, 0


def bump_major(major: int, _minor: int, _patch: int) -> tuple[int, int, int]:
    return major + 1, 0, 0


def read(path: Path | None = None) -> tuple[int, int, int]:
    p = path or VERSION_PATH
    if not p.is_file():
        return 1, 0, 0
    return parse(p.read_text(encoding="utf-8"))


def write(major: int, minor: int, patch: int, path: Path | None = None) -> None:
    p = path or VERSION_PATH
    p.write_text(format_ver(major, minor, patch) + "\n", encoding="utf-8")
