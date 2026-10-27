#!/usr/bin/env python3
"""GATE-01 host environment check for DashCue.

Runs on Windows, Linux and macOS (DEC-23: host tooling is cross-platform Python,
never shell-only scripts). It answers one question for the operator: *can this
machine build and flash everything this project needs?*

Design rules:
  * Never claim success it did not measure. A check that cannot run reports
    BLOCKED with the reason, not PASS.
  * Missing *optional* tools (e.g. adb, when the HUD is the only target) do not
    fail the run; missing *required* tools do.
  * Exit code 0 = all required checks PASS, 1 = at least one required check
    FAILED or BLOCKED. Warnings alone still exit 0.

Evidence: this script's full output is stored in
docs/evidence/GATE-01-setup-check.txt as part of the M0 gate record.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from collections.abc import Sequence
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

MIN_PYTHON = (3, 10)
TARGET_ESP32_PLATFORM = "espressif32"
TARGET_ESP32_VERSION = "7.1.3"
REQUIRED_BOARD = "cyd_2432s028r"
GRADLE_MIN_MAJOR = 8
COMPILE_SDK = 36
MIN_SDK = 26

PASS = "PASS"
FAIL = "FAIL"
WARN = "WARN"
BLOCKED = "BLOCKED"


@dataclass
class Check:
    name: str
    status: str
    detail: str = ""
    required: bool = True
    hints: list[str] = field(default_factory=list)


def run(cmd: Sequence[str], timeout: int = 60) -> tuple[int, str]:
    """Run a command, returning (exit_code, combined_output). Never raises.

    Encoding: tool output is UTF-8 (PlatformIO prints box-drawing characters) but
    a Windows console defaults to a legacy code page. Decoding with
    errors="replace" and forcing PYTHONIOENCODING on the child keeps a stray
    character from turning a passing check into a crash - which is exactly what
    happened here before this was fixed.
    """
    env = {**os.environ, "PYTHONIOENCODING": "utf-8"}
    try:
        proc = subprocess.run(
            list(cmd),
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
            encoding="utf-8",
            errors="replace",
            env=env,
        )
    except FileNotFoundError:
        return 127, "command not found"
    except subprocess.TimeoutExpired:
        return 124, f"timed out after {timeout}s"
    except OSError as exc:  # pragma: no cover - platform specific
        return 126, str(exc)
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")


def first_version(text: str) -> str:
    m = re.search(r"(\d+\.\d+(?:\.\d+)?)", text)
    return m.group(1) if m else ""


def check_python() -> Check:
    v = sys.version_info
    detail = f"{v.major}.{v.minor}.{v.micro} at {sys.executable}"
    if (v.major, v.minor) >= MIN_PYTHON:
        return Check("python", PASS, detail)
    return Check(
        "python",
        FAIL,
        detail,
        hints=[f"need Python >= {MIN_PYTHON[0]}.{MIN_PYTHON[1]}"],
    )


def check_platformio() -> Check:
    exe = shutil.which("pio") or shutil.which("platformio")
    if not exe:
        return Check(
            "platformio",
            FAIL,
            "pio not on PATH",
            hints=["pip install -U platformio", "then re-run this script"],
        )
    code, out = run([exe, "--version"])
    if code != 0:
        return Check("platformio", FAIL, f"`pio --version` failed: {out.strip()[:200]}")
    return Check("platformio", PASS, f"{out.strip()} ({exe})")


def check_esp32_platform() -> Check:
    """Look for the exact pinned platform among the globally installed ones.

    `pio platform list` is deprecated (and its output was the crash above), so
    this uses `pio pkg list -g`. Several platform versions may be installed for
    other projects, which is why the exact pin is matched rather than any match.
    """
    exe = shutil.which("pio") or shutil.which("platformio")
    if not exe:
        return Check("esp32_platform", BLOCKED, "platformio core unavailable")
    code, out = run([exe, "pkg", "list", "-g"], timeout=180)
    if code != 0:
        return Check("esp32_platform", BLOCKED, f"`pio pkg list -g` failed: {out.strip()[:200]}")

    pattern = re.compile(
        rf"{re.escape(TARGET_ESP32_PLATFORM)}\s*@\s*{re.escape(TARGET_ESP32_VERSION)}\b"
    )
    installed = sorted(set(re.findall(r"espressif32\s*@\s*([\d.]+)", out)))
    if pattern.search(out):
        return Check(
            "esp32_platform",
            PASS,
            f"{TARGET_ESP32_PLATFORM} @ {TARGET_ESP32_VERSION} installed (also present: "
            + (", ".join(v for v in installed if v != TARGET_ESP32_VERSION) or "none")
            + ")",
        )
    return Check(
        "esp32_platform",
        FAIL,
        f"{TARGET_ESP32_PLATFORM} @ {TARGET_ESP32_VERSION} not installed (found: "
        + (", ".join(installed) or "none")
        + ")",
        hints=[f"pio platform install {TARGET_ESP32_PLATFORM}@{TARGET_ESP32_VERSION}"],
    )


def check_board_profile() -> Check:
    # Spec §18 puts the board profiles in boards/ and the partition table in
    # firmware/ (PlatformIO resolves board_build.partitions relative to the
    # project directory, not the board file).
    board = REPO_ROOT / "boards" / f"{REQUIRED_BOARD}.json"
    parts = REPO_ROOT / "firmware" / "partitions.csv"
    if not board.exists():
        return Check("board_profile", FAIL, f"missing {board.relative_to(REPO_ROOT)}")
    if not parts.exists():
        return Check("board_profile", FAIL, f"missing {parts.relative_to(REPO_ROOT)}")
    try:
        data = json.loads(board.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return Check("board_profile", FAIL, f"{board.name} is not valid JSON: {exc}")
    # PlatformIO resolves build.partitions relative to the *project* directory
    # (firmware/), not the board file, so the board manifest carries a relative
    # path back into boards/. Compare basenames rather than the whole string.
    declared = str(data.get("build", {}).get("partitions", ""))
    if Path(declared).name != parts.name:
        return Check(
            "board_profile",
            FAIL,
            f"board declares partitions={declared!r}, expected a path ending in {parts.name}",
        )
    return Check(
        "board_profile",
        PASS,
        f"{board.name} ({data.get('name','?')}) + {parts.name}",
    )


def check_serial_ports() -> Check:
    exe = shutil.which("pio") or shutil.which("platformio")
    if not exe:
        return Check("serial_ports", BLOCKED, "platformio core unavailable")
    code, out = run([exe, "device", "list"], timeout=120)
    if code != 0:
        return Check("serial_ports", BLOCKED, f"`pio device list` failed: {out.strip()[:200]}")

    # Count only real ports: PlatformIO prints a header block per port then dashes.
    ports = re.findall(r"^(COM\d+|/dev/(?:ttyUSB|ttyACM|cu\.)\S+)", out, flags=re.MULTILINE)
    detail = ", ".join(ports) if ports else "no serial ports detected"
    if ports:
        return Check("serial_ports", PASS, detail, required=False)
    # The board may legitimately be absent (spec §0.5: that is a valid blocker).
    return Check(
        "serial_ports",
        WARN,
        detail,
        required=False,
        hints=[
            "connect the CYD board by USB and re-run",
            "on Windows install the CH340/CP210x driver",
            "on Linux add your user to the dialout group",
        ],
    )


def check_java() -> Check:
    if not shutil.which("java"):
        return Check(
            "java",
            FAIL,
            "java not on PATH",
            hints=["install JDK 17 (Temurin recommended)"],
        )
    code, out = run(["java", "-version"])
    text = out.strip().splitlines()[0] if out.strip() else ""
    m = re.search(r'version "(\d+)', out)
    major = int(m.group(1)) if m else 0
    if code == 0 and major >= 17:
        return Check("java", PASS, f"{text} (major {major})")
    return Check(
        "java",
        FAIL,
        f"{text} (major {major})",
        hints=["AGP 8.13 requires JDK 17 or newer"],
    )


def check_android_sdk() -> Check:
    """Locate the SDK and confirm the exact platform/build-tools we compile against."""
    candidates = []
    for env_name in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        val = os.environ.get(env_name)
        if val:
            candidates.append(Path(val))
    home = Path.home()
    candidates += [
        home / "AppData" / "Local" / "Android" / "Sdk",  # Windows
        home / "Library" / "Android" / "sdk",            # macOS
        home / "Android" / "Sdk",                        # Linux
    ]

    sdk = next((c for c in candidates if c.is_dir()), None)
    if sdk is None:
        return Check(
            "android_sdk",
            FAIL,
            "Android SDK not found in ANDROID_HOME/ANDROID_SDK_ROOT or the usual locations",
            hints=[
                "install Android Studio, or",
                "sdkmanager 'platforms;android-36' 'build-tools;36.0.0' 'platform-tools'",
            ],
        )

    platform_dir = sdk / "platforms" / f"android-{COMPILE_SDK}"
    if not platform_dir.is_dir():
        installed = sorted(p.name for p in (sdk / "platforms").glob("android-*")) if (sdk / "platforms").is_dir() else []
        return Check(
            "android_sdk",
            FAIL,
            f"platform android-{COMPILE_SDK} missing under {sdk} (installed: {', '.join(installed) or 'none'})",
            hints=[f"sdkmanager \"platforms;android-{COMPILE_SDK}\""],
        )

    return Check("android_sdk", PASS, f"{sdk} with android-{COMPILE_SDK}")


def check_adb() -> Check:
    exe = shutil.which("adb")
    if not exe:
        return Check(
            "adb",
            WARN,
            "adb not on PATH",
            required=False,
            hints=["install Android platform-tools to install the APK on a phone"],
        )
    code, out = run([exe, "devices", "-l"])
    if code != 0:
        return Check("adb", WARN, f"`adb devices` failed: {out.strip()[:200]}", required=False)
    devices = [ln for ln in out.splitlines()[1:] if ln.strip() and not ln.startswith("*")]
    detail = f"{len(devices)} device(s): " + ("; ".join(d.strip() for d in devices) if devices else "none")
    return Check("adb", PASS if devices else WARN, detail, required=False)


def check_repo_layout() -> Check:
    needed = [
        "protocol/dashcue.proto",
        "protocol/dashcue.options",
        "protocol/uuids.md",
        "firmware/platformio.ini",
        "firmware/partitions.csv",
        "firmware/src/main.cpp",
        "boards/cyd_2432s028r.json",
        "tools/hw_probe/platformio.ini",
        "tools/hw_probe/src/main.cpp",
        "tools/gen_proto.py",
    ]
    missing = [p for p in needed if not (REPO_ROOT / p).exists()]
    if missing:
        return Check("repo_layout", FAIL, "missing: " + ", ".join(missing))
    return Check("repo_layout", PASS, f"{len(needed)} required files present")


def check_git() -> Check:
    if not shutil.which("git"):
        return Check("git", FAIL, "git not on PATH", hints=["install git"])
    code, out = run(["git", "-C", str(REPO_ROOT), "rev-parse", "--is-inside-work-tree"])
    if code != 0 or "true" not in out:
        return Check("git", WARN, "not a git work tree", required=False)
    # A repository with no commits yet has no HEAD to resolve; report that as
    # informational instead of letting git's error look like a failed check.
    code, _ = run(["git", "-C", str(REPO_ROOT), "rev-parse", "--verify", "HEAD"])
    if code != 0:
        return Check("git", PASS, "work tree present, no commits yet (unborn HEAD)", required=False)
    code, branch = run(["git", "-C", str(REPO_ROOT), "rev-parse", "--abbrev-ref", "HEAD"])
    return Check("git", PASS, f"work tree on branch {branch.strip()}", required=False)


CHECKS = (
    check_python,
    check_platformio,
    check_esp32_platform,
    check_board_profile,
    check_serial_ports,
    check_java,
    check_android_sdk,
    check_adb,
    check_repo_layout,
    check_git,
)


def evaluate_checks(checks=CHECKS) -> list[Check]:
    """An inaccessible tool must not hide the remaining environment checks."""
    results = []
    for fn in checks:
        try:
            results.append(fn())
        except OSError as exc:
            results.append(Check(fn.__name__.removeprefix("check_"), BLOCKED,
                                 f"Cannot inspect from this process: {exc}"))
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description="DashCue GATE-01 host environment check")
    parser.add_argument("--quiet", action="store_true", help="only print the summary and failures")
    args = parser.parse_args()

    print("DashCue setup_check (GATE-01)")
    print(f"host     : {platform.system()} {platform.release()} ({platform.machine()})")
    print(f"python   : {sys.version.split()[0]}")
    print(f"repo     : {REPO_ROOT}")
    print("-" * 68)

    checks = evaluate_checks()

    width = max(len(c.name) for c in checks)
    for c in checks:
        if args.quiet and c.status == PASS:
            continue
        label = c.name.ljust(width)
        print(f"[{c.status:7}] {label}  {c.detail}")
        for hint in c.hints:
            print(f"{'':17}  -> {hint}")

    print("-" * 68)
    counts: dict[str, int] = {}
    for c in checks:
        counts[c.status] = counts.get(c.status, 0) + 1
    print("summary: " + ", ".join(f"{k}={v}" for k, v in sorted(counts.items())))

    blocking = [c for c in checks if c.required and c.status in (FAIL, BLOCKED)]
    if blocking:
        print("GATE-01: FAIL (required checks not satisfied: "
              + ", ".join(c.name for c in blocking) + ")")
        return 1

    warnings = [c for c in checks if c.status in (WARN, BLOCKED)]
    if warnings:
        print("GATE-01: PASS with warnings (" + ", ".join(c.name for c in warnings) + ")")
    else:
        print("GATE-01: PASS")
    print(json.dumps({"gate": "GATE-01", "status": "PASS", "warnings": [c.name for c in warnings]}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
