"""Cross-platform native test runner for PlatformIO's `native` environment.

PlatformIO executes this script instead of invoking a shell wrapper directly,
so the same runners work on Windows and Linux CI (GitHub Actions has no
PowerShell). The compiled Unity test program lives under .pio/build/native;
the resolved binary is executed and its exit status is propagated so `pio
test` reports pass/fail accurately.
"""

import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent
BUILD_DIR = PROJECT_DIR / ".pio" / "build" / "native"

CANDIDATES = ["program.exe", "program"]


def main() -> int:
    for candidate in CANDIDATES:
        executable = BUILD_DIR / candidate
        if executable.is_file():
            return subprocess.run([str(executable)]).returncode
    print(
        f"Native test executable not found under {BUILD_DIR}; "
        "does the native test environment build successfully?",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
