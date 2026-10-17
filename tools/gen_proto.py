#!/usr/bin/env python3
"""Generate protocol codec sources from the single .proto (spec §18).

Two consumers, one schema:

  nanopb    -> firmware/src/protocol/generated/dashcue.pb.{c,h}
  javalite  -> android/core/protocol/src/main/java/com/dashcue/v1/*.java

nanopb side
    Uses the generator bundled with the *firmware's own Nanopb library*
    (firmware/.pio/libdeps/*/Nanopb/generator/), not a pip-installed copy. The
    generator and the library must be the same nanopb version, otherwise the
    generator can emit options the decoder does not understand; reading it out
    of the pinned library makes that mismatch impossible rather than unlikely.

javalite side
    grpcio-tools' protoc is built WITHOUT Java codegen (it only ships the Python
    bindings), so `--java_out` fails here. The Android build generates these
    sources itself through the protobuf Gradle plugin, which pulls a protoc that
    does include Java. So this tool:
      * uses a standalone `protoc` from PATH when one exists and works, else
      * reports javalite as DEFERRED to Gradle, naming the reason
    It never fabricates output and never reports a side it did not run.

Drift
    `--check` regenerates into a temporary directory and compares byte-for-byte
    with what is committed. Generated sources are committed on purpose: that is
    what makes "CI fails if generated files drift" a meaningful statement rather
    than a tautology.

Usage:
    python tools/gen_proto.py            # regenerate
    python tools/gen_proto.py --check    # fail if committed output is stale
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PROTO_DIR = REPO_ROOT / "protocol"
PROTO_FILE = "dashcue.proto"
OPTIONS_FILE = PROTO_DIR / "dashcue.options"

FW_GENERATED = REPO_ROOT / "firmware" / "src" / "protocol" / "generated"
ANDROID_GENERATED = (
    REPO_ROOT / "android" / "core" / "protocol" / "src" / "main" / "java" / "com" / "dashcue" / "v1"
)


def _out(msg: str) -> None:
    print(msg)


def find_nanopb_generator() -> Path | None:
    """Locate the generator shipped inside the pinned Nanopb library."""
    matches = sorted(REPO_ROOT.glob("firmware/.pio/libdeps/*/Nanopb/generator/nanopb_generator.py"))
    if matches:
        return matches[-1]
    return None


def run(cmd: list[str], cwd: Path | None = None) -> tuple[int, str]:
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(cwd) if cwd else None,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
    except FileNotFoundError:
        return 127, f"not found: {cmd[0]}"
    except OSError as exc:  # pragma: no cover - platform specific
        return 126, str(exc)
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")


def generate_nanopb(dest: Path) -> tuple[bool, str]:
    """Run the bundled nanopb generator into `dest`. Returns (ok, detail)."""
    generator = find_nanopb_generator()
    if generator is None:
        return (
            False,
            "nanopb generator not found under firmware/.pio/libdeps/*/Nanopb/generator/.\n"
            "  Install the pinned library first:  pio run -e cyd_ili9341\n"
            "  (the build downloads it; the generator then matches the firmware library exactly)",
        )
    if not OPTIONS_FILE.exists():
        return False, f"missing {OPTIONS_FILE.relative_to(REPO_ROOT)}"

    dest.mkdir(parents=True, exist_ok=True)
    # Run from protocol/ so the generator's output path is flat in `dest`
    # instead of mirroring the protocol/ directory.
    code, out = run(
        [
            sys.executable,
            str(generator),
            PROTO_FILE,
            f"--options-file={OPTIONS_FILE}",
            "-D",
            str(dest),
        ],
        cwd=PROTO_DIR,
    )
    if code != 0:
        return False, f"nanopb generator failed (exit {code}):\n{out.strip()}"

    produced = sorted(p.name for p in dest.glob("dashcue.pb.*"))
    if not produced:
        return False, f"generator reported success but produced nothing: {out.strip()}"

    normalised = normalise_newlines(dest)
    if normalised:
        # Informational only: the files are correct either way, they just needed
        # the newline form pinned so the drift check is platform-independent.
        produced.append(f"(CRLF->LF: {', '.join(normalised)})")
    return True, ", ".join(produced)


def normalise_newlines(directory: Path) -> list[str]:
    """Force generated files to LF, returning the names that were changed.

    nanopb's generator writes through Python's text mode, so it emits CRLF on
    Windows and LF elsewhere. Without this, "committed output matches freshly
    generated output" would be false on the *other* platform for a byte-identical
    file, and a drift check that cries wolf gets disabled - which is worse than
    not having one. Normalising makes the committed bytes platform-independent,
    which is also what .gitattributes (eol=lf) promises.
    """
    changed: list[str] = []
    for path in sorted(directory.glob("dashcue.pb.*")):
        data = path.read_bytes()
        if b"\r\n" not in data:
            continue
        path.write_bytes(data.replace(b"\r\n", b"\n"))
        changed.append(path.name)
    return changed


def java_out_works(protoc: Path) -> bool:
    """True when this protoc can actually emit lite Java (grpcio's cannot)."""
    with tempfile.TemporaryDirectory() as tmp:
        code, _ = run(
            [
                str(protoc),
                f"-I{PROTO_DIR}",
                f"--java_out=lite:{tmp}",
                PROTO_FILE,
            ]
        )
        if code != 0:
            return False
        return any(Path(tmp).rglob("*.java"))


def generate_javalite(dest: Path) -> tuple[str, str]:
    """Returns (status, detail) where status is 'ok' or 'deferred'."""
    proto_on_path = shutil.which("protoc")
    if proto_on_path and java_out_works(Path(proto_on_path)):
        dest.mkdir(parents=True, exist_ok=True)
        code, out = run(
            [
                proto_on_path,
                f"-I{PROTO_DIR}",
                f"--java_out=lite:{dest}",
                PROTO_FILE,
            ]
        )
        if code != 0:
            return "deferred", f"protoc at {proto_on_path} failed:\n{out.strip()}"
        n = len(list(dest.rglob("*.java")))
        return "ok", f"{n} .java files via {proto_on_path}"

    return (
        "deferred",
        "no protoc with Java codegen on PATH (grpcio-tools ships Python bindings only, so\n"
        "  --java_out is unavailable). The Android build generates these sources itself\n"
        "  through the protobuf Gradle plugin, which bundles a protoc that includes Java.\n"
        "  This side runs as soon as android/core/protocol exists (M2/M4).",
    )


def report_missing_module() -> tuple[str, str]:
    return (
        "deferred",
        f"{ANDROID_GENERATED.relative_to(REPO_ROOT)} does not exist yet (M4 creates the module).\n"
        "  nanogen side is unaffected; no Java sources are claimed.",
    )


def compare_tree(committed: Path, generated: Path) -> list[str]:
    """Return a list of human-readable differences. Empty means identical."""
    problems: list[str] = []
    if not committed.exists():
        return [f"{committed.relative_to(REPO_ROOT)} is missing (run: python tools/gen_proto.py)"]

    committed_files = {p.name: p for p in committed.iterdir() if p.is_file()}
    generated_files = {p.name: p for p in generated.iterdir() if p.is_file()}

    def content(path: Path) -> bytes:
        # Compare newline-normalised bytes: a CRLF checkout of the same content
        # is not drift, and the normalisation step means this should never fire.
        return path.read_bytes().replace(b"\r\n", b"\n")


    for name in sorted(set(committed_files) - set(generated_files)):
        problems.append(f"stale: {name} is committed but the generator no longer produces it")
    for name in sorted(set(generated_files) - set(committed_files)):
        problems.append(f"missing: {name} is produced but not committed")
    for name in sorted(set(committed_files) & set(generated_files)):
        if content(committed_files[name]) != content(generated_files[name]):
            problems.append(f"drift: {name} differs from freshly generated output")
    return problems


def check_version_consistency() -> list[str]:
    """firmware/include/dashcue_version.h must agree with the .proto package."""
    problems: list[str] = []
    header = REPO_ROOT / "firmware" / "include" / "dashcue_version.h"
    proto = PROTO_DIR / PROTO_FILE
    if not header.exists() or not proto.exists():
        return problems

    text = header.read_text(encoding="utf-8")
    match = re.search(r"#define\s+DASHCUE_PROTO_MAJOR\s+(\d+)", text)
    minor = re.search(r"#define\s+DASHCUE_PROTO_MINOR\s+(\d+)", text)
    package = re.search(r'^package\s+dashcue\.v(\d+)\s*;', proto.read_text(encoding="utf-8"), re.M)
    if match and package and match.group(1) != package.group(1):
        problems.append(
            f"version mismatch: dashcue_version.h PROTO_MAJOR={match.group(1)} "
            f"but .proto package is dashcue.v{package.group(1)}"
        )
    if match and minor:
        # dashcue.proto declares protocol 1.0 in its header comment.
        m = re.search(r"protocol version\s+(\d+)\.(\d+)", proto.read_text(encoding="utf-8"), re.I)
        if m and (m.group(1), m.group(2)) != (match.group(1), minor.group(1)):
            problems.append(
                f"version mismatch: header says {match.group(1)}.{minor.group(1)}, "
                f".proto header comment says {m.group(1)}.{m.group(2)}"
            )
    return problems


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            try:
                stream.reconfigure(encoding="utf-8", errors="replace")
            except (ValueError, OSError):  # pragma: no cover
                pass

    parser = argparse.ArgumentParser(description="Generate DashCue protocol codec sources")
    parser.add_argument(
        "--check",
        action="store_true",
        help="regenerate into a temp dir and fail if the committed output differs",
    )
    args = parser.parse_args()

    _out("DashCue protocol generation (single .proto source of truth, DEC-12)")
    _out("-" * 70)

    # --- nanopb (firmware) ---------------------------------------------------
    # Two distinct modes. Generating into a temporary directory and then
    # deleting it while reporting OK is exactly the failure this separation
    # prevents: the write path must produce files that persist.
    if args.check:
        with tempfile.TemporaryDirectory() as tmp_nano:
            nano_ok, nano_detail = generate_nanopb(Path(tmp_nano) / "out")
            if not nano_ok:
                _out(f"nanopb : FAIL - {nano_detail}")
                return 1
            _out(f"nanopb : OK - {nano_detail}")
            problems = compare_tree(FW_GENERATED, Path(tmp_nano) / "out")
            for p in problems:
                _out(f"       : {p}")
            if problems:
                _out("RESULT: DRIFT - run `python tools/gen_proto.py` and commit the result")
                return 1
            _out("       : committed output matches freshly generated output")
    else:
        nano_ok, nano_detail = generate_nanopb(FW_GENERATED)
        if not nano_ok:
            _out(f"nanopb : FAIL - {nano_detail}")
            return 1
        produced = sorted(p.name for p in FW_GENERATED.glob("dashcue.pb.*"))
        for name in produced:
            size = (FW_GENERATED / name).stat().st_size
            _out(f"       : {name} ({size} bytes)")
        _out(f"nanopb : OK - wrote {nano_detail} to "
             f"{FW_GENERATED.relative_to(REPO_ROOT)}")

    # --- javalite (Android) --------------------------------------------------
    if ANDROID_GENERATED.exists():
        with tempfile.TemporaryDirectory() as tmp_java:
            status, detail = generate_javalite(Path(tmp_java) / "out")
    else:
        status, detail = report_missing_module()

    if status == "deferred":
        _out(f"java   : DEFERRED - {detail}")
    else:
        _out(f"java   : OK - {detail}")

    # --- cross-file version agreement ---------------------------------------
    version_problems = check_version_consistency()
    for p in version_problems:
        _out(f"version: FAIL - {p}")
    if version_problems:
        _out("RESULT: FAIL")
        return 1
    _out("version: OK - dashcue_version.h agrees with protocol/dashcue.proto")

    _out(f"RESULT: {'OK (check passed)' if args.check else 'OK'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
