#!/usr/bin/env python3
"""Build current production parsers and run bounded, seeded HOST fuzz checks.

Never reuse the historical prebuilt executable. This does not exercise nanopb's
decoder, BLE hardware, Android, or on-device memory/latency budgets.
"""

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

PROJECT_ROOT = Path(__file__).resolve().parents[3]


def run_campaign(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-only", action="store_true", help="Do not run Python oracle tests")
    parser.add_argument("--iterations", type=int, default=1000000)
    parser.add_argument("--compiler", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--evidence-dir", type=Path)
    args = parser.parse_args(argv)
    if not 100 <= args.iterations <= 10000000:
        parser.error("--iterations must be between 100 and 10000000")
    now = dt.datetime.now(dt.timezone.utc)
    local_date = now.astimezone(dt.timezone(dt.timedelta(hours=5, minutes=30))).strftime("%Y%m%d")
    parent = args.evidence_dir or PROJECT_ROOT / "docs/evidence" / local_date / "host-parser-fuzz"
    evidence = parent.resolve() / ("native-fuzz-" + now.strftime("%Y%m%dT%H%M%S-%fZ"))
    evidence.mkdir(parents=True, exist_ok=False)
    sources = [
        "tools/test-only/fuzz/c_protocol_fuzz.cpp",
        "firmware/src/protocol/crc32.cpp", "firmware/src/protocol/framing.cpp",
        "firmware/src/protocol/fragmentation.cpp", "firmware/src/protocol/dedup_cache.cpp",
        "firmware/src/protocol/generated/dashcue.pb.c",
        "firmware/.pio/libdeps/native/Nanopb/pb_common.c",
        "firmware/.pio/libdeps/native/Nanopb/pb_decode.c",
        "firmware/.pio/libdeps/native/Nanopb/pb_encode.c",
    ]
    report = {"scope": "production C++ framing/reassembly/dedupe on host; no hardware or nanopb decoder fuzzing",
              "started_utc": now.isoformat(), "seed": "0xC001CAFE", "iterations": args.iterations,
              "hardware_exercised": False, "status": "BLOCKED"}
    exit_code = 2
    try:
        compiler = shutil.which(args.compiler)
        if not compiler:
            raise FileNotFoundError(f"Compiler not found: {args.compiler}")
        exe = evidence / ("c_protocol_fuzz.exe" if os.name == "nt" else "c_protocol_fuzz")
        includes = ["firmware/src", "firmware/src/protocol/generated", "firmware/.pio/libdeps/native/Nanopb"]
        command = [compiler, "-std=c++17", "-Wall", "-Wextra"] + ["-I" + str(PROJECT_ROOT / p) for p in includes]
        command += [str(PROJECT_ROOT / p) for p in sources] + ["-o", str(exe)]
        inputs = set(PROJECT_ROOT / p for p in sources)
        inputs.update((PROJECT_ROOT / "firmware/src/protocol").rglob("*.h"))
        inputs.update((PROJECT_ROOT / "firmware/.pio/libdeps/native/Nanopb").glob("*.h"))
        inputs.add(PROJECT_ROOT / "protocol/fixtures/fixtures_data.h")
        report["input_sha256"] = {str(p.relative_to(PROJECT_ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(inputs)}
        report["compile_command"] = command
        build = subprocess.run(command, cwd=PROJECT_ROOT, capture_output=True, text=True, timeout=120)
        (evidence / "build.log").write_text(build.stdout + build.stderr, encoding="utf-8")
        if build.returncode:
            report.update(status="FAIL", reason="Native compilation failed", compile_exit_code=build.returncode)
            exit_code = 1
        else:
            report["executable_sha256"] = hashlib.sha256(exe.read_bytes()).hexdigest()
            native = subprocess.run([str(exe), str(args.iterations)], cwd=PROJECT_ROOT, capture_output=True, text=True, timeout=120)
            (evidence / "native.log").write_text(native.stdout + native.stderr, encoding="utf-8")
            print(native.stdout)
            print(native.stderr, file=sys.stderr)
            exit_code = native.returncode
            report.update(status="PASS" if exit_code == 0 else "FAIL", native_exit_code=exit_code)
            if not args.native_only:
                oracle = subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "tests/adversarial", "-v"],
                                        cwd=PROJECT_ROOT, capture_output=True, text=True, timeout=120)
                (evidence / "python-oracles.log").write_text(oracle.stdout + oracle.stderr, encoding="utf-8")
                report["python_oracle_exit_code"] = oracle.returncode
                if oracle.returncode:
                    report.update(status="FAIL", reason="Python oracle suite did not pass")
                    exit_code = 1
    except (OSError, subprocess.TimeoutExpired) as error:
        report.update(status="BLOCKED", reason=str(error))
        exit_code = 2
    (evidence / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Host parser fuzz result: {report['status']}; evidence: {evidence}")
    return exit_code


if __name__ == "__main__":
    sys.exit(run_campaign())
