"""Check that an inaccessible SDK cannot abort the rest of the environment report."""
import importlib.util
import sys
import unittest
from pathlib import Path

source = Path(__file__).resolve().parents[2] / "setup_check.py"
spec = importlib.util.spec_from_file_location("setup_check", source)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)


class SetupReportTests(unittest.TestCase):
    def test_permission_error_is_blocked_and_next_check_runs(self):
        def check_sdk():
            raise PermissionError("access denied")

        result = module.evaluate_checks([check_sdk, lambda: module.Check("next", module.PASS, "ran")])
        self.assertEqual([check.status for check in result], [module.BLOCKED, module.PASS])
        self.assertEqual(result[0].name, "sdk")
        self.assertTrue(result[0].required)


if __name__ == "__main__":
    unittest.main()
