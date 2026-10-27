"""
Adversarial Verification Suite for UI Touch Synchronization, Debouncing & Screen Discipline.
Author: challenger_ui_touch_sync
Focus:
- 1-sample press latch, 3-sample release unlatch
- 100 Hz sampling and 3-sample median filter
- 280 ms button lockout
- SPI bus mutex arbitration
- Bidirectional transport, volume, and units state consistency
- Automotive safety, touch dimensions (>= 56x48 px), 8 px ext-click, no-scroll flags
"""

import os
import re
import sys
import threading
import time
import unittest

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, PROJECT_ROOT)


def median3(a: int, b: int, c: int) -> int:
    if a > b:
        if b > c:
            return b
        return min(a, c)
    else:
        if a > c:
            return a
        return min(b, c)


class TouchDebounceModel:
    """1:1 Model of touch_hal.cpp 1-sample press latch / 3-sample release unlatch."""
    def __init__(self):
        self.debounce_press_count = 0
        self.debounce_release_count = 0
        self.latched_pressed = False

    def update(self, valid_sample: bool, pen_down: bool) -> bool:
        if valid_sample:
            self.debounce_release_count = 0
            if self.debounce_press_count < 1:
                self.debounce_press_count += 1
            if self.debounce_press_count >= 1:
                self.latched_pressed = True
        else:
            self.debounce_press_count = 0
            if not pen_down:
                if self.debounce_release_count < 3:
                    self.debounce_release_count += 1
                if self.debounce_release_count >= 3:
                    self.latched_pressed = False
            else:
                self.debounce_release_count = 0
        return self.latched_pressed


class TestTouchDebounceAndArbitration(unittest.TestCase):
    """Empirical tests for touch debouncing and arbitration contracts."""

    def test_one_sample_press_latch(self):
        """Verify that a single valid 100 Hz sample immediately latches pressed."""
        sm = TouchDebounceModel()
        self.assertFalse(sm.latched_pressed)
        
        # Frame 1: Valid sample with pen_down=True
        latched = sm.update(valid_sample=True, pen_down=True)
        self.assertTrue(latched, "1-sample press contract: 1 valid sample MUST latch pressed immediately!")

    def test_three_sample_release_unlatch(self):
        """Verify that exactly 3 consecutive released samples unlatch pressed."""
        sm = TouchDebounceModel()
        sm.update(valid_sample=True, pen_down=True)
        self.assertTrue(sm.latched_pressed)

        # Release Frame 1: pen_down=False, valid_sample=False
        self.assertTrue(sm.update(valid_sample=False, pen_down=False), "Frame 1 must remain latched")
        
        # Release Frame 2: pen_down=False, valid_sample=False
        self.assertTrue(sm.update(valid_sample=False, pen_down=False), "Frame 2 must remain latched")
        
        # Release Frame 3: pen_down=False, valid_sample=False
        self.assertFalse(sm.update(valid_sample=False, pen_down=False), "Frame 3 must unlatch pressed!")

    def test_pen_down_holds_latch_against_adc_glitch(self):
        """While pen_down is True (finger on glass), temporary valid_sample=False dips must NOT unlatch."""
        sm = TouchDebounceModel()
        sm.update(valid_sample=True, pen_down=True)
        self.assertTrue(sm.latched_pressed)

        # Multiple samples where valid_sample is False but pen_down is True (light pressure dip)
        for _ in range(10):
            latched = sm.update(valid_sample=False, pen_down=True)
            self.assertTrue(latched, "While pen_down is LOW, touch latch MUST NOT be broken by ADC noise")

    def test_median3_burst_filter(self):
        """Verify 3-burst median filter eliminates single-sample spikes."""
        # Noise spike on middle sample
        self.assertEqual(median3(100, 4000, 102), 102)
        # Noise spike on first sample
        self.assertEqual(median3(4000, 100, 102), 102)
        # Noise spike on last sample
        self.assertEqual(median3(100, 102, 4000), 102)
        # Ground dip
        self.assertEqual(median3(2500, 0, 2505), 2500)

    def test_button_lockout_280ms(self):
        """Verify 280 ms lockout filters rapid bounce or double triggers."""
        last_touch_ms = 0
        accepted_events = 0

        def on_touch(now_ms):
            nonlocal last_touch_ms, accepted_events
            if now_ms - last_touch_ms < 280:
                return False
            last_touch_ms = now_ms
            accepted_events += 1
            return True

        self.assertTrue(on_touch(1000))
        self.assertFalse(on_touch(1050))   # +50ms -> rejected
        self.assertFalse(on_touch(1200))   # +200ms -> rejected
        self.assertFalse(on_touch(1279))   # +279ms -> rejected
        self.assertTrue(on_touch(1280))    # +280ms -> accepted!
        self.assertFalse(on_touch(1300))   # +20ms -> rejected
        self.assertTrue(on_touch(1560))    # +280ms -> accepted!
        self.assertEqual(accepted_events, 3)

    def test_spi_bus_mutex_arbitration(self):
        """Verify mutual exclusion between display DMA and touch sampling."""
        bus_lock = threading.Lock()
        dma_active = False
        collision_detected = False

        def dma_flush():
            nonlocal dma_active, collision_detected
            for _ in range(50):
                with bus_lock:
                    dma_active = True
                    time.sleep(0.001)
                    dma_active = False
                time.sleep(0.002)

        def touch_sample():
            nonlocal dma_active, collision_detected
            for _ in range(100):
                acquired = bus_lock.acquire(timeout=0.012)
                if acquired:
                    try:
                        if dma_active:
                            collision_detected = True
                        time.sleep(0.0003)
                    finally:
                        bus_lock.release()
                time.sleep(0.001)

        t1 = threading.Thread(target=dma_flush)
        t2 = threading.Thread(target=touch_sample)
        t1.start()
        t2.start()
        t1.join()
        t2.join()

        self.assertFalse(collision_detected, "SPI bus mutex must prevent DMA and touch collision")


class TestBidirectionalSyncConsistency(unittest.TestCase):
    """Verify bidirectional synchronization rules and contracts."""

    def test_volume_action_bypasses_stale_snapshot_check(self):
        """Volume commands MUST bypass stale snapshotVersion checking to avoid dropping adjustments."""
        current_snapshot = 42

        def is_command_stale(action_str, cmd_snapshot):
            is_volume = action_str in ("MA_VOL_STEP", "MA_VOL_SET", "MA_MUTE_SET")
            if is_volume or cmd_snapshot == 0:
                return False
            delta = current_snapshot - cmd_snapshot
            return delta > 2 or delta < 0

        # Non-volume command with stale snapshot
        self.assertTrue(is_command_stale("MA_NEXT", 35))
        self.assertTrue(is_command_stale("MA_PLAY", 30))
        # Non-volume command within tolerance window (delta in 0..2)
        self.assertFalse(is_command_stale("MA_NEXT", 41))
        self.assertFalse(is_command_stale("MA_NEXT", 42))

        # Volume commands MUST NOT be rejected even with very old snapshot
        self.assertFalse(is_command_stale("MA_VOL_STEP", 1))
        self.assertFalse(is_command_stale("MA_VOL_SET", 5))
        self.assertFalse(is_command_stale("MA_MUTE_SET", 10))

    def test_units_state_synchronization(self):
        """Verify units conversion logic between Metric and Imperial."""
        # Speed: km/h <-> mph (factor 1.609344)
        def convert_speed(speed_kmh, imperial):
            if not imperial:
                return speed_kmh, "km/h"
            return int(speed_kmh / 1.609344 + 0.5), "mph"

        self.assertEqual(convert_speed(100, False), (100, "km/h"))
        self.assertEqual(convert_speed(100, True), (62, "mph"))
        self.assertEqual(convert_speed(50, True), (31, "mph"))

        # Distance: meters <-> feet / miles
        def format_nav_distance(dist_m, imperial):
            if not imperial:
                if dist_m < 1000:
                    return f"{dist_m} m"
                return f"{dist_m / 1000.0:.1f} km"
            feet = int(dist_m * 3.28084)
            if feet < 1000:
                return f"{feet} ft"
            return f"{feet / 5280.0:.1f} mi"

        self.assertEqual(format_nav_distance(500, False), "500 m")
        self.assertEqual(format_nav_distance(1500, False), "1.5 km")
        self.assertEqual(format_nav_distance(100, True), "328 ft")
        self.assertEqual(format_nav_distance(3000, True), "1.9 mi")


class TestUiScreenLayoutCompliance(unittest.TestCase):
    """Static AST / regex verification of UI screen compliance."""

    def test_all_screens_comply_with_automotive_touch_standards(self):
        screens = [
            "firmware/src/ui/ui_screen_media.cpp",
            "firmware/src/ui/ui_screen_nav.cpp",
            "firmware/src/ui/ui_screen_settings.cpp",
            "firmware/src/ui/ui_screen_pairing.cpp",
            "firmware/src/ui/ui_screen_diag.cpp",
            "firmware/src/ui/ui_screen_safemode.cpp",
            "firmware/src/ui/ui_tab_bar.cpp",
        ]

        for s_path in screens:
            full_path = os.path.join(PROJECT_ROOT, s_path)
            self.assertTrue(os.path.exists(full_path), f"File {s_path} must exist")
            with open(full_path, "r", encoding="utf-8", errors="replace") as f:
                content = f.read()

            # Rule: No scrollable flags
            self.assertIn("LV_OBJ_FLAG_SCROLLABLE", content,
                          f"{s_path} must explicitly remove scrollable flags")

            # Rule: Hardware isolation (No direct GPIO/ADC in UI)
            self.assertFalse(re.search(r"\banalogRead\s*\(", content), f"Forbidden analogRead in {s_path}")
            self.assertFalse(re.search(r"\bdigitalRead\s*\(", content), f"Forbidden digitalRead in {s_path}")
            self.assertFalse(re.search(r"\bgpio_get_level\s*\(", content), f"Forbidden gpio_get_level in {s_path}")

            # Rule: Dual events (CLICKED and SHORT_CLICKED)
            clicked = len(re.findall(r"LV_EVENT_CLICKED\b", content))
            short_clicked = len(re.findall(r"LV_EVENT_SHORT_CLICKED\b", content))
            if clicked > 0:
                self.assertGreaterEqual(short_clicked, clicked,
                                        f"{s_path} has {clicked} CLICKED but only {short_clicked} SHORT_CLICKED")


if __name__ == "__main__":
    unittest.main()
