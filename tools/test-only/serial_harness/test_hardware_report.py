"""Host regressions for evidence classification; these do not contact hardware."""
import importlib.util
import unittest
from pathlib import Path

source = Path(__file__).resolve().parents[2] / "run_hardware_checks.py"
spec = importlib.util.spec_from_file_location("hardware_checks", source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def states(log):
    return {row["check"]: row["status"] for row in module.analyze_serial(log)}


def sample(seconds, free=55000, minimum=49000, largest=26000):
    return (f"STATS uptime_s={seconds} heap_free={free} heap_min={minimum} "
            f"largest_block={largest} render_px_per_s=1000\n")


class EvidenceClassificationTests(unittest.TestCase):
    def test_empty_capture_never_passes(self):
        self.assertNotIn("PASS", states("").values())

    def test_short_capture_does_not_pass_stability(self):
        result = states("BOOT boot_to_ui_ms=1700\n" + sample(5))
        self.assertEqual(result["bounded_runtime_stability"], "NOT_RUN")

    def test_bounded_pass_does_not_claim_physical_touch(self):
        result = states("BOOT boot_to_ui_ms=1700\n" + sample(5) + sample(25))
        self.assertEqual(result["bounded_runtime_stability"], "PASS")
        self.assertEqual(result["heap_budget_in_capture"], "PASS")
        self.assertEqual(result["physical_display_and_touch"], "NOT_RUN")

    def test_transient_low_heap_fails_even_if_recovered(self):
        result = states(sample(5, minimum=20000) + sample(25))
        self.assertEqual(result["heap_budget_in_capture"], "FAIL")

    def test_fragmented_heap_fails(self):
        self.assertEqual(states(sample(5, largest=3000))["heap_budget_in_capture"], "FAIL")

    def test_second_boot_detected(self):
        result = states("BOOT boot_to_ui_ms=1700\n" + sample(25) + "BOOT boot_to_ui_ms=1700\n" + sample(5))
        self.assertEqual(result["bounded_runtime_stability"], "FAIL")
        self.assertEqual(result["boot_complete"], "FAIL")

    def test_crash_after_boot_fails(self):
        result = states("BOOT boot_to_ui_ms=1700\n" + sample(5) + sample(25) + "Guru Meditation Error\n")
        self.assertEqual(result["bounded_runtime_stability"], "FAIL")

    def test_missing_memory_field_does_not_pass(self):
        result = states("STATS uptime_s=5 heap_free=99000 largest_block=99000\n")
        self.assertEqual(result["heap_budget_in_capture"], "NOT_RUN")


if __name__ == "__main__":
    unittest.main()
