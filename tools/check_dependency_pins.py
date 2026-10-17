#!/usr/bin/env python3
"""Assert the build files still contain exactly the GATE-08 pinned versions.

The spec's rule 4 says a dependency may only be adopted or upgraded through
GATE-08, with the result recorded in docs/DEPENDENCIES.md. This script is what
makes that rule enforceable instead of aspirational: it fails if a build file
drifts from the recorded pin, so an accidental `^` range or a hand-edited
version cannot slip through unnoticed.

It reads the pins out of the build files (it does not trust them) and compares
against the table below, which mirrors docs/DEPENDENCIES.md.

Usage:  python tools/check_dependency_pins.py
Exit:   0 = every present file matches; 1 = drift or an unparsable file.
        A file that does not exist yet is reported PENDING (not a failure),
        because later milestones create it.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# The report contains § characters; a Windows console defaulting to a legacy
# code page would abort the script instead of printing them.
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        try:
            _stream.reconfigure(encoding="utf-8", errors="replace")
        except (ValueError, OSError):  # pragma: no cover - platform dependent
            pass

REPO_ROOT = Path(__file__).resolve().parent.parent

PLATFORMIO_INI = REPO_ROOT / "firmware" / "platformio.ini"
VERSION_CATALOG = REPO_ROOT / "android" / "gradle" / "libs.versions.toml"

# --- firmware pins: dependency string as it must appear in lib_deps ---------
FIRMWARE_PINS = {
    "bodmer/TFT_eSPI": "2.5.43",
    "lvgl/lvgl": "9.6.0",
    "nanopb/Nanopb": "0.4.92",
}
# The platform is pinned with `platform = <name> @ <version>`, a different
# syntax from lib_deps entries, so it is checked separately.
FIRMWARE_PLATFORM = ("espressif32", "7.1.3")
# Pinned by git tag: the registry publishes no versioned release for this library.
FIRMWARE_GIT_PINS = {
    "XPT2046_Touchscreen": "https://github.com/PaulStoffregen/XPT2046_Touchscreen.git#v1.4",
}

# --- android pins: key in libs.versions.toml -> exact expected version ------
ANDROID_PINS = {
    "agp": "8.13.2",
    "kotlin": "2.3.21",
    "composeBom": "2026.06.01",
    "ksp": "2.3.12",
    "room": "2.8.5",
    "protobufJavalite": "4.36.2",
    "protobufPlugin": "0.10.0",
    "maplibre": "13.7.1",
    "coreKtx": "1.19.1",
    "activityCompose": "1.13.0",
    "lifecycle": "2.11.0",
    "navigationCompose": "2.10.2",
    "datastore": "1.2.1",
    "workRuntime": "2.12.0",
    "junitExt": "1.3.0",
    "testRunner": "1.7.0",
    "mockk": "1.14.11",
    "coroutinesTest": "1.11.0",
    "robolectric": "4.17",
    "compileSdk": "36",
    "targetSdk": "36",
    "minSdk": "26",
}


def parse_toml_like_versions(text: str) -> dict[str, str]:
    """Parse a minimal `[versions]` block: key = "value" with an optional comment."""
    versions: dict[str, str] = {}
    in_versions = False
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("["):
            in_versions = line == "[versions]"
            continue
        if not in_versions or "=" not in line:
            continue
        key, _, value = line.partition("=")
        versions[key.strip()] = value.strip().strip('"').strip("'")
    return versions


def check_firmware() -> list[str]:
    problems: list[str] = []
    if not PLATFORMIO_INI.exists():
        return [f"PENDING {PLATFORMIO_INI.relative_to(REPO_ROOT)} does not exist"]

    text = PLATFORMIO_INI.read_text(encoding="utf-8")

    for name, version in FIRMWARE_PINS.items():
        expected = f"{name}@{version}"
        if expected in text:
            continue
        # Distinguish "absent" from "present at a different version" - the second
        # is drift and must be loud.
        loose = re.findall(rf"{re.escape(name)}@([^\s]+)", text)
        if loose:
            problems.append(f"DRIFT   {name}: pinned @{', @'.join(loose)} but {version} is recorded")
        else:
            problems.append(f"MISSING {name}@{version} not present in platformio.ini")

    platform_name, platform_version = FIRMWARE_PLATFORM
    if not re.search(
        rf"^\s*platform\s*=\s*{re.escape(platform_name)}\s*@\s*{re.escape(platform_version)}\s*$",
        text,
        flags=re.MULTILINE,
    ):
        loose = re.findall(r"^\s*platform\s*=\s*([^\s]+)(?:\s*@\s*([^\s]+))?", text, flags=re.MULTILINE)
        seen = ", ".join(f"{n}@{v or 'unpinned'}" for n, v in loose) or "none"
        problems.append(
            f"DRIFT   platform: expected {platform_name} @ {platform_version}, found {seen}"
        )

    for name, git_url in FIRMWARE_GIT_PINS.items():
        if git_url in text:
            continue
        if re.search(rf"{re.escape(name)}", text) or git_url.split("#")[0] in text:
            problems.append(f"DRIFT   {name}: git pin is not exactly {git_url}")
        else:
            problems.append(f"MISSING {name} git pin {git_url}")

    for m in re.finditer(r"^\s*(?:lib_deps|build_flags)\s*=.*$", text, flags=re.MULTILINE):
        if "@^" in m.group(0) or "@~" in m.group(0):
            problems.append(f"RANGE   caret/tilde range found: {m.group(0).strip()}")

    return problems


def check_android() -> list[str]:
    if not VERSION_CATALOG.exists():
        return [f"PENDING {VERSION_CATALOG.relative_to(REPO_ROOT)} does not exist (created in M4)"]

    found = parse_toml_like_versions(VERSION_CATALOG.read_text(encoding="utf-8"))
    problems: list[str] = []
    for key, expected in ANDROID_PINS.items():
        actual = found.get(key)
        if actual is None:
            problems.append(f"MISSING {key} is not declared in libs.versions.toml")
        elif actual != expected:
            problems.append(f"DRIFT   {key}: catalog says {actual}, DEPENDENCIES.md records {expected}")
    return problems


def main() -> int:
    print("DashCue dependency pin check (GATE-08)")
    print("-" * 68)
    problems = check_firmware() + check_android()
    pending = [p for p in problems if p.startswith("PENDING")]
    real = [p for p in problems if not p.startswith("PENDING")]

    for p in problems:
        print(p)

    print("-" * 68)
    if real:
        print(f"RESULT: DRIFT ({len(real)} problem(s)); update the build files or re-run GATE-08")
        return 1
    if pending:
        print(f"RESULT: OK with {len(pending)} file(s) not created yet")
        return 0
    print("RESULT: OK - build files match the recorded pins")
    return 0


if __name__ == "__main__":
    sys.exit(main())
