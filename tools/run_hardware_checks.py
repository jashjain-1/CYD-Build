#!/usr/bin/env python3
"""Capture physical HUD/phone bring-up evidence; never substitute a simulator.

Requires pyserial for the serial subprocess and adb for phone checks. Resets the
HUD through DTR/RTS, but does not erase, flash, install, pair, or change permissions.
These checks are a bring-up gate, not full product E2E certification.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def check(name, status, detail):
    return {"check": name, "status": status, "detail": detail}


def analyze_serial(log: str, minimum_span: int = 20) -> list[dict]:
    """Only assess bounded serial observations; pixels sent are not panel proof."""
    stats = []
    for line in log.splitlines():
        if line.startswith("STATS "):
            stats.append({key: int(value) for key, value in re.findall(r"(\w+)=(-?\d+)", line)})
    boot = re.findall(r"^BOOT boot_to_ui_ms=(\d+)", log, re.M)
    crashes = re.findall(r"^.*(?:Guru Meditation|abort\(\)|assert failed|BOOT FAILED|INIT FAILED|Brownout detector|Stack canary watchpoint|stack overflow).*$", log, re.M | re.I)
    times = [row["uptime_s"] for row in stats if "uptime_s" in row]
    span = times[-1] - times[0] if len(times) >= 2 else 0
    resets = any(b <= a for a, b in zip(times, times[1:])) or len(boot) > 1
    output = [
        check("boot_complete", "PASS" if len(boot) == 1 and not crashes else "FAIL",
              {"boot_to_ui_ms": [int(t) for t in boot], "fault_lines": crashes}),
        check("bounded_runtime_stability", "FAIL" if crashes or resets else "PASS" if span >= minimum_span else "NOT_RUN",
              {"observed_stats_span_s": span, "required_span_s": minimum_span, "reset_detected": resets}),
    ]
    memory = [row for row in stats if all(key in row for key in ("heap_free", "heap_min", "largest_block"))]
    if memory:
        lowest = {key: min(row[key] for row in memory) for key in ("heap_free", "heap_min", "largest_block")}
        ok = lowest["heap_min"] >= 40 * 1024 and lowest["largest_block"] >= 20 * 1024
        output.append(check("heap_budget_in_capture", "PASS" if ok else "FAIL", lowest))
    else:
        output.append(check("heap_budget_in_capture", "NOT_RUN", "No complete memory telemetry"))
    pixels = [row.get("render_px_per_s", 0) for row in stats]
    output.append(check("panel_transfer_observed", "PASS" if any(value > 0 for value in pixels) else "NOT_RUN",
                        "Transfer counters do not prove visible colors, panel identity, or animation FPS"))
    output.append(check("physical_display_and_touch", "NOT_RUN",
                        "Requires eyes on panel and five real target presses; serial telemetry alone cannot pass this"))
    return output


def run(command: list[str], timeout: int = 30) -> tuple[int, str]:
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=timeout)
        return result.returncode, result.stdout + result.stderr
    except (OSError, subprocess.TimeoutExpired) as exc:
        return 126, str(exc)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Explicit physical HUD serial port, e.g. COM4")
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--adb", default=shutil.which("adb"))
    parser.add_argument("--phone", help="ADB serial (required when more than one physical phone is attached)")
    parser.add_argument("--package", default="com.dashcue.app")
    parser.add_argument("--out", type=Path, help="New evidence directory; existing files are never overwritten")
    args = parser.parse_args()
    if args.seconds < 25:
        parser.error("--seconds must be at least 25 for multiple runtime samples")
    instant = dt.datetime.now(dt.timezone.utc)
    out = args.out or ROOT / "docs" / "evidence" / "hardware-runs" / instant.strftime("%Y%m%dT%H%M%S%fZ")
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    records = []
    command = [sys.executable, str(ROOT / "tools/test-only/serial_harness/capture.py"),
               "--port", args.port, "--seconds", str(args.seconds), "--out", str(out / "hud-serial.log")]
    code, text = run(command, args.seconds + 15)
    (out / "capture-command.log").write_text(text, encoding="utf-8")
    records.append(check("serial_capture", "PASS" if code == 0 else "BLOCKED" if code in (2, 126) else "FAIL",
                         {"command": command, "exit_code": code}))
    serial_file = out / "hud-serial.log"
    if serial_file.exists():
        records.extend(analyze_serial(serial_file.read_text(encoding="utf-8")))
    if not args.adb:
        records.append(check("physical_phone", "BLOCKED", "adb not found; supply --adb"))
    else:
        code, listing = run([args.adb, "devices", "-l"])
        (out / "adb-devices.log").write_text(listing, encoding="utf-8")
        phones = re.findall(r"^(\S+)\s+device\s.*$", listing, re.M)
        phones = [phone for phone in phones if not phone.startswith("emulator-")]
        selected = args.phone if args.phone in phones else phones[0] if not args.phone and len(phones) == 1 else None
        if code or selected is None:
            records.append(check("physical_phone", "BLOCKED", "No unique authorized physical phone; inspect adb-devices.log"))
        else:
            base = [args.adb, "-s", selected, "shell"]
            emulation = [run(base + ["getprop", prop]) for prop in ("ro.kernel.qemu", "ro.boot.qemu")]
            physical = all(code == 0 for code, _ in emulation) and all(value.strip() != "1" for _, value in emulation)
            records.append(check("physical_phone", "PASS" if physical else "BLOCKED",
                                 {"serial": selected, "emulator_properties": emulation,
                                  "basis": "Authorized ADB target with no Android emulator flag"}))
            for prop in ("ro.product.model", "ro.build.version.release", "ro.build.version.sdk"):
                status, value = run(base + ["getprop", prop])
                records.append(check(prop, "PASS" if status == 0 and value.strip() else "BLOCKED", value.strip()))
            status, package = run(base + ["dumpsys", "package", args.package])
            (out / "phone-package.log").write_text(package, encoding="utf-8")
            installed = status == 0 and "versionName=" in package
            records.append(check("app_installed", "PASS" if installed else "FAIL", args.package))
            flags = re.findall(r"(?:pkgFlags|flags)=\[([^\]]*)\]", package)
            debug = any("DEBUGGABLE" in row.split() for row in flags)
            records.append(check("release_debuggable_disabled", "FAIL" if debug else "PASS" if installed and flags else "NOT_RUN", flags))
    for name in ("secure_pairing_and_sync", "real_media_commands", "navigation_with_gps", "reconnect_10_cycles", "eight_hour_soak", "in_vehicle_readability"):
        records.append(check(name, "NOT_RUN", "Requires a separate physical scenario and evidence"))
    report = {"started_utc": instant.isoformat(), "scope": "physical bring-up, not full E2E",
              "overall": "FAIL" if any(row["status"] == "FAIL" for row in records) else "INCOMPLETE",
              "checks": records}
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    print(f"Evidence: {out.resolve()}")
    return 1 if report["overall"] == "FAIL" else 2


if __name__ == "__main__":
    raise SystemExit(main())
