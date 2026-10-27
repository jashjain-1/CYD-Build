#!/usr/bin/env python3
"""Capture serial output from the HUD into an evidence file (test-only tooling).

Why a tool and not `pio device monitor`: evidence has to be reproducible and
machine-checkable. This writes a timestamped log, prints a clear summary of any
`RESULT`/`FAIL` lines it saw, and exits non-zero if a line says FAIL - so a
failing hardware probe cannot be mistaken for a passing one.

Test-only location: `tools/test-only/` is excluded from release artefacts
(DEC-26).

Usage:
    python tools/test-only/serial_harness/capture.py --port COM4 --seconds 12 \
        --out docs/evidence/20261016/TC-HW-01/hw_probe.log
    python tools/test-only/serial_harness/capture.py --list
"""

from __future__ import annotations

import argparse
import datetime as dt
import sys
from pathlib import Path

try:
    import serial  # provided by the pyserial dependency of PlatformIO
    from serial.tools import list_ports
except ImportError:  # pragma: no cover - environment dependent
    print("pyserial is required: python -m pip install pyserial", file=sys.stderr)
    raise SystemExit(2)

REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent


def list_serial_ports() -> int:
    ports = list(list_ports.comports())
    if not ports:
        print("no serial ports found")
        return 1
    for p in ports:
        print(f"{p.device}\t{p.description}\t{p.hwid}")
    return 0


def main() -> int:
    # Device output is arbitrary bytes. On Windows the console defaults to a
    # legacy code page that cannot encode every decoded character, which would
    # abort the capture mid-run; force UTF-8 with replacement instead so a stray
    # byte in a log never destroys the evidence.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            try:
                stream.reconfigure(encoding="utf-8", errors="replace")
            except (ValueError, OSError):  # pragma: no cover - platform dependent
                pass

    parser = argparse.ArgumentParser(description="Capture HUD serial output as evidence")
    parser.add_argument("--port", help="serial port, e.g. COM4 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=float, default=10.0)
    parser.add_argument("--out", help="log file to write (default: stdout only)")
    parser.add_argument("--list", action="store_true", help="list serial ports and exit")
    # A reset is right for capturing a boot banner and wrong for recording a
    # session that is already in progress: it discards the wizard state the run
    # was meant to observe.
    parser.add_argument("--no-reset", action="store_true",
                        help="do not pulse DTR/RTS; record the running session as-is")
    args = parser.parse_args()

    if args.list:
        return list_serial_ports()
    if not args.port:
        parser.error("--port is required (or use --list)")

    out_path = Path(args.out) if args.out else None
    if out_path is not None and not out_path.is_absolute():
        out_path = REPO_ROOT / out_path
    if out_path is not None:
        out_path.parent.mkdir(parents=True, exist_ok=True)

    lines: list[str] = []
    # A probe run is only meaningful with the port, limits and time recorded, so
    # the header goes into the evidence file as well as to the console.
    header = (
        f"# DashCue serial capture\n"
        f"# port={args.port} baud={args.baud} seconds={args.seconds}\n"
        f"# out={args.out or 'stdout'}\n"
        f"# started={dt.datetime.now().isoformat(timespec='seconds')}\n"
    )
    print(header, end="")

    serial_error = None
    try:
        ser = serial.Serial()
        ser.port = args.port
        ser.baudrate = args.baud
        ser.timeout = 0.2
        ser.dtr = False
        ser.rts = False
        if not args.no_reset:
            # Reset the board into a known state so the boot banner is captured
            ser.rts = True
        ser.open()
        with ser:
            import time

            if args.no_reset:
                # Leave running firmware alone
                time.sleep(0.1)
                ser.reset_input_buffer()
            else:
                time.sleep(0.1)
                ser.rts = False
            deadline = time.time() + args.seconds
            while time.time() < deadline:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                if not line:
                    continue
                lines.append(line)
                print(line)
    except serial.SerialException as exc:
        serial_error = exc
        print(f"serial error on {args.port}: {exc}", file=sys.stderr)
    finally:
        # Write whatever was captured even when COM4 flaps mid-run: a port drop
        # is exactly the failure being investigated, so the evidence must
        # survive it instead of vanishing with the exception.
        if out_path is not None:
            out_path.write_text(header + "\n".join(lines) + "\n", encoding="utf-8")
            print(f"\n# wrote {len(lines)} lines to {out_path.resolve()}")

    if serial_error is not None:
        print(f"# capture ENDED EARLY by serial error after {len(lines)} lines")
        return 2

    failures = [ln for ln in lines if "FAIL" in ln.upper()]
    results = [ln for ln in lines if ln.startswith("RESULT ")]
    print(f"# RESULT lines: {results.__len__()}")
    for ln in results:
        print(f"#   {ln}")
    if failures:
        print(f"# {len(failures)} FAIL line(s) present - this capture is NOT a pass:")
        for ln in failures:
            print(f"#   {ln}")
        return 1
    if not lines:
        print("# no output captured - check the port, the driver and the baud rate")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
