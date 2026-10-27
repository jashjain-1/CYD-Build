"""Stdlib regressions for truthful host simulation reporting."""

import importlib.util
import unittest
from pathlib import Path
from types import SimpleNamespace

spec = importlib.util.spec_from_file_location("simulation_runner", Path(__file__).parent / "e2e/run_e2e_tests.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class SimulationReportingTest(unittest.TestCase):
    def setUp(self):
        self.collector = runner.E2ECollectorPlugin()

    def emit(self, when, outcome):
        self.collector.pytest_runtest_logreport(SimpleNamespace(
            nodeid="tests/e2e/test_tier1_features.py::case", when=when,
            passed=outcome == "passed", failed=outcome == "failed", skipped=outcome == "skipped"))

    def test_setup_failure_is_counted(self):
        self.emit("setup", "failed")
        self.assertEqual(self.collector.tier_counts["tier1"]["failed"], 1)

    def test_teardown_failure_replaces_call_pass(self):
        self.emit("call", "passed")
        self.emit("teardown", "failed")
        counts = self.collector.tier_counts["tier1"]
        self.assertEqual((counts["total"], counts["passed"], counts["failed"]), (1, 0, 1))

    def test_setup_skip_is_counted(self):
        self.emit("setup", "skipped")
        self.assertEqual(self.collector.tier_counts["tier1"]["skipped"], 1)

    def test_partial_tier_cannot_pass(self):
        self.emit("call", "passed")
        self.assertEqual(runner.result_status(0, self.collector.tier_counts, "1"), "INCOMPLETE")

    def test_complete_tier_and_collection_failure(self):
        counts = self.collector.tier_counts
        counts["tier1"].update(passed=115, total=115)
        self.assertEqual(runner.result_status(0, counts, "1"), "PASS")
        self.assertEqual(runner.result_status(2, counts, "1"), "FAIL")
        counts["tier1"].update(skipped=1, total=116)
        self.assertEqual(runner.result_status(0, counts, "1"), "INCOMPLETE")


if __name__ == "__main__":
    unittest.main()
