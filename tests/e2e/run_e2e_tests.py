#!/usr/bin/env python3
"""Run the legacy four-tier HOST SIMULATION suite.

These tests use Python models and some source/literal assertions. They do not
connect to a HUD, exercise Android, or satisfy the real-device TC-E2E cases.
The filename is retained for compatibility with existing invocations.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
E2E_TESTS_DIR = REPO_ROOT / "tests" / "e2e"
TIER_MINIMUMS = {"tier1": 115, "tier2": 115, "tier3": 23, "tier4": 12}
INDIA_TZ = dt.timezone(dt.timedelta(hours=5, minutes=30))


class E2ECollectorPlugin:
    """Count a case once, including setup/teardown failures and skips."""

    def __init__(self):
        self.outcomes = {}

    def pytest_runtest_logreport(self, report):
        if not any(f"test_{tier}_" in report.nodeid for tier in TIER_MINIMUMS):
            return
        previous = self.outcomes.get(report.nodeid)
        if report.failed:
            self.outcomes[report.nodeid] = "failed"
        elif report.skipped and previous != "failed":
            self.outcomes[report.nodeid] = "skipped"
        elif report.when == "call" and report.passed and previous is None:
            self.outcomes[report.nodeid] = "passed"

    @property
    def tier_counts(self):
        counts = {tier: dict(passed=0, failed=0, skipped=0, total=0) for tier in TIER_MINIMUMS}
        for nodeid, outcome in self.outcomes.items():
            for tier, values in counts.items():
                if f"test_{tier}_" in nodeid:
                    values[outcome] += 1
                    values["total"] += 1
        return counts


def result_status(exit_code, counts, tier):
    if exit_code != 0 or any(values["failed"] for values in counts.values()):
        return "FAIL"
    selected = TIER_MINIMUMS if tier == "all" else {f"tier{tier}": TIER_MINIMUMS[f"tier{tier}"]}
    if any(counts[name]["skipped"] or counts[name]["passed"] < minimum for name, minimum in selected.items()):
        return "INCOMPLETE"
    return "PASS"


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="DashCue host simulation runner (no hardware)")
    parser.add_argument("--tier", choices=["1", "2", "3", "4", "all"], default="all")
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument("--evidence-dir", type=Path, help="New or existing parent for a unique run directory")
    args = parser.parse_args(argv)
    now = dt.datetime.now(dt.timezone.utc)
    parent = args.evidence_dir or REPO_ROOT / "docs/evidence" / now.astimezone(INDIA_TZ).strftime("%Y%m%d") / "host-simulation"
    evidence = parent.resolve() / now.strftime("%H%M%S-%fZ")
    evidence.mkdir(parents=True, exist_ok=False)
    collector = E2ECollectorPlugin()
    report = {
        "suite": "host-simulation", "started_utc": now.isoformat(),
        "hardware_exercised": False, "android_exercised": False,
        "real_device_e2e_status": "NOT RUN", "tier": args.tier,
        "scope": "Python models, source checks and literal assertions; not device or release certification",
        "python": sys.executable,
    }
    print("DashCue HOST SIMULATION suite; no HUD or Android device is exercised.", flush=True)
    try:
        import pytest
    except ImportError as error:
        exit_code = 2
        status = "BLOCKED"
        report["reason"] = str(error)
    else:
        paths = [str(p) for p in sorted(E2E_TESTS_DIR.glob(f"test_tier{'*' if args.tier == 'all' else args.tier}_*.py"))]
        pytest_args = paths + ["-v" if args.verbose else "-q", f"--junitxml={evidence / 'result.xml'}"]
        # Several legacy source checks resolve paths against the repository root.
        import os
        previous_cwd = Path.cwd()
        try:
            os.chdir(REPO_ROOT)
            exit_code = int(pytest.main(pytest_args, plugins=[collector])) if paths else 5
        finally:
            os.chdir(previous_cwd)
        status = result_status(exit_code, collector.tier_counts, args.tier)
    report.update(status=status, pytest_exit_code=exit_code, tiers=collector.tier_counts)
    (evidence / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Host simulation result: {status}; real-device E2E: NOT RUN")
    print(f"Evidence: {evidence}")
    return 0 if status == "PASS" else (2 if status == "BLOCKED" else 1)


if __name__ == "__main__":
    sys.exit(main())
