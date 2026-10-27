"""Integration verification for firmware UI Screen Automotive Touch Discipline.

Runs tools/audit_ui_screens.py across all 7 UI screens and asserts 100% compliance:
- Rule 1: Widget dimensions >= 56x48 px
- Rule 2: Extended click area padding >= 8 px
- Rule 3: Dual event registration (LV_EVENT_CLICKED + LV_EVENT_SHORT_CLICKED)
- Rule 4: No scrollable flags (LV_OBJ_FLAG_SCROLLABLE and scroll-chains removed)
- Rule 5: Hardware isolation (zero direct analogRead, digitalRead, or raw ADC in UI)
"""

import os
import subprocess
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def test_ui_screens_static_audit():
    """Verify that tools/audit_ui_screens.py passes with exit code 0."""
    audit_script = os.path.join(REPO_ROOT, "tools", "audit_ui_screens.py")
    assert os.path.exists(audit_script), f"Missing audit script: {audit_script}"

    res = subprocess.run(
        [sys.executable, audit_script, "--json"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    assert res.returncode == 0, f"Audit failed:\nSTDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}"


def test_ui_screens_strict_audit():
    """Verify that strict audit passes across all files."""
    audit_script = os.path.join(REPO_ROOT, "tools", "audit_ui_screens.py")
    res = subprocess.run(
        [sys.executable, audit_script, "--strict"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    assert res.returncode == 0, f"Strict audit failed:\n{res.stdout}"
    assert "100% COMPLIANT - ALL SCREENS PASSED" in res.stdout
