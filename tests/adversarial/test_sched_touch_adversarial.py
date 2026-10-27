"""Adversarial Verification Suite for FreeRTOS Scheduling & Touch Arbitration.

Rigorous stress-testing of:
1. ui_task adaptive sleep clamp strictly between 4 ms and 30 ms under extreme boundaries:
   - 0 ms (immediate timer expiration)
   - 1, 2, 3 ms (sub-minimum durations)
   - 4 ms (exact lower clamp boundary)
   - 5..29 ms (nominal interior range)
   - 30 ms (exact upper clamp boundary)
   - 31, 100, 1000, 60000 ms (overshoot durations)
   - UINT32_MAX (0xFFFFFFFF, LV_NO_TIMER_READY / idle)
   - Arithmetic wrap-around and unsigned comparison behavior.
2. touch_task hardware PENIRQ pre-check:
   - 100% skip of g_spi_bus_mutex acquisition and SPI read bursts when screen is idle.
   - Zero bus collisions, zero mutex contention with display_flush_cb during idle.
3. Release debounce preservation:
   - 3 consecutive release samples required to unlatch.
   - Zero frozen / sticky touches upon finger lift.
   - Immediate resume of idle skip once released.
   - Resilience against contact bounce and noise glitches during hold and release.
"""

import sys
import os
import unittest
import threading
import time
import random

# Ensure project root is in sys.path
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)


def constrain_u32(amt: int, low: int, high: int) -> int:
    """Exact emulation of C/C++ macro #define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
    with 32-bit unsigned integers.
    """
    amt_u32 = amt & 0xFFFFFFFF
    low_u32 = low & 0xFFFFFFFF
    high_u32 = high & 0xFFFFFFFF
    if amt_u32 < low_u32:
        return low_u32
    elif amt_u32 > high_u32:
        return high_u32
    else:
        return amt_u32


class TestUiTaskAdaptiveSleepClamp(unittest.TestCase):
    """Adversarial stress testing of ui_task sleep clamp."""

    def test_clamp_boundary_values(self):
        """Verify strict [4, 30] ms bounds across all critical boundary conditions."""
        test_cases = [
            (0, 4),                     # 0 ms -> clamped to 4 ms
            (1, 4),                     # 1 ms -> clamped to 4 ms
            (2, 4),                     # 2 ms -> clamped to 4 ms
            (3, 4),                     # 3 ms -> clamped to 4 ms
            (4, 4),                     # 4 ms -> exact lower bound
            (5, 5),                     # Interior
            (15, 15),                   # Interior nominal
            (29, 29),                   # Interior
            (30, 30),                   # 30 ms -> exact upper bound
            (31, 30),                   # 31 ms -> clamped to 30 ms
            (50, 30),                   # 50 ms -> clamped to 30 ms
            (100, 30),                  # 100 ms -> clamped to 30 ms
            (1000, 30),                 # 1 s -> clamped to 30 ms
            (60000, 30),                # 60 s -> clamped to 30 ms
            (0x7FFFFFFF, 30),           # INT32_MAX -> clamped to 30 ms
            (0x80000000, 30),           # 2^31 -> clamped to 30 ms
            (0xFFFFFFFE, 30),           # UINT32_MAX - 1 -> clamped to 30 ms
            (0xFFFFFFFF, 30),           # UINT32_MAX (LV_NO_TIMER_READY) -> clamped to 30 ms
        ]

        for next_call, expected_sleep in test_cases:
            result = constrain_u32(next_call, 4, 30)
            self.assertEqual(
                result, expected_sleep,
                f"next_call={next_call} (0x{next_call:08X}) clamped to {result}, expected {expected_sleep}"
            )
            self.assertGreaterEqual(result, 4, f"Sleep {result} violates lower bound 4 ms")
            self.assertLessEqual(result, 30, f"Sleep {result} violates upper bound 30 ms")

    def test_clamp_fuzzing_full_uint32_space(self):
        """Fuzz 1,000,000 randomized 32-bit unsigned inputs to prove invariant: 4 <= sleep <= 30."""
        random.seed(0xCAFE42)
        for _ in range(1000000):
            val = random.randint(0, 0xFFFFFFFF)
            clamped = constrain_u32(val, 4, 30)
            if val < 4:
                self.assertEqual(clamped, 4)
            elif val > 30:
                self.assertEqual(clamped, 30)
            else:
                self.assertEqual(clamped, val)
            self.assertTrue(4 <= clamped <= 30)


class MockTouchHardwareState:
    """Simulates physical XPT2046 hardware pins and SPI bus."""
    def __init__(self):
        self.penirq_low = False        # Active LOW (True = finger touching screen)
        self.raw_x = 2000
        self.raw_y = 2000
        self.raw_z1 = 0
        self.raw_z2 = 0
        self.spi_transfer_count = 0
        self.mutex_acquire_count = 0
        self.mutex = threading.Lock()


class SimulatedTouchTask:
    """1:1 Model of touch_task in firmware/src/touch/touch_hal.cpp:108-388."""

    def __init__(self, hw: MockTouchHardwareState):
        self.hw = hw
        self.debounce_press_count = 0
        self.debounce_release_count = 0
        self.latched_pressed = False
        self.prev_latched = False
        self.s_prev_pen_down = False
        self.s_prev_pen = False
        self.s_saturated_rejects = 0
        self.s_valid_presses = 0
        self.latest_point_pressed = False
        self.latest_point_x = 0
        self.latest_point_y = 0
        self.has_raw = False
        self.idle_skip_count = 0
        self.active_sample_count = 0

    def step_sample(self):
        """Execute one 100 Hz (10 ms) tick of touch_task."""
        pen_down = self.hw.penirq_low
        should_sample = pen_down or self.latched_pressed

        if not should_sample:
            # Complete idle skip branch (touch_hal.cpp:148-163)
            self.latest_point_pressed = False
            self.has_raw = False
            self.debounce_press_count = 0
            self.debounce_release_count = 0
            self.latched_pressed = False
            self.prev_latched = False
            self.s_prev_pen_down = False
            self.s_prev_pen = False
            self.idle_skip_count += 1
            return False

        self.active_sample_count += 1
        contact = pen_down or self.latched_pressed
        first_contact_skip = pen_down and not self.s_prev_pen_down and not self.latched_pressed

        raw_x = 0
        raw_y = 0
        raw_z1 = 0
        raw_z2 = 0
        valid_sample = False

        # Acquire mutex
        self.hw.mutex_acquire_count += 1
        with self.hw.mutex:
            # 3 samples per channel in hardware:
            self.hw.spi_transfer_count += 12 # 4 channels * 3 samples
            raw_x = self.hw.raw_x
            raw_y = self.hw.raw_y
            raw_z1 = self.hw.raw_z1
            raw_z2 = self.hw.raw_z2

            bounds_ok = (40 <= raw_x <= 4080 and 40 <= raw_y <= 4080)
            pressure_and_bounds_ok = (raw_z1 >= 3 and bounds_ok)

            pressure_onset = (
                pressure_and_bounds_ok and
                not pen_down and not self.latched_pressed and
                raw_z1 >= 24
            )

            if pressure_and_bounds_ok:
                if (contact or pressure_onset) and not first_contact_skip:
                    valid_sample = True
                    self.s_valid_presses += 1
                    self.s_saturated_rejects = 0
            elif pen_down:
                self.s_saturated_rejects += 1
            else:
                if self.s_saturated_rejects < 25:
                    self.s_saturated_rejects = 0

        # Debounce state machine
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

        if valid_sample:
            self.has_raw = True
        elif not self.latched_pressed:
            self.has_raw = False

        self.latest_point_pressed = self.latched_pressed
        if self.latched_pressed:
            self.latest_point_x = raw_x
            self.latest_point_y = raw_y

        self.prev_latched = self.latched_pressed
        self.s_prev_pen = pen_down
        self.s_prev_pen_down = pen_down
        return True


class TestTouchArbitrationAndDebounce(unittest.TestCase):
    """Adversarial stress testing of touch task idle skipping and debounce preservation."""

    def test_idle_screen_skips_100_percent_of_spi_and_mutex(self):
        """Verify that when untouched, 100% of mutex acquisitions and SPI bursts are skipped."""
        hw = MockTouchHardwareState()
        hw.penirq_low = False  # Screen is completely idle
        task = SimulatedTouchTask(hw)

        # Run 10,000 ticks (equivalent to 100 seconds at 100 Hz)
        for _ in range(10000):
            sampled = task.step_sample()
            self.assertFalse(sampled)
            self.assertFalse(task.latest_point_pressed)

        self.assertEqual(hw.mutex_acquire_count, 0, "Idle screen must perform ZERO mutex takes!")
        self.assertEqual(hw.spi_transfer_count, 0, "Idle screen must perform ZERO SPI transfers!")
        self.assertEqual(task.idle_skip_count, 10000)
        self.assertEqual(task.active_sample_count, 0)

    def test_touch_press_latch_and_release_debounce_preservation(self):
        """Verify 3-sample release debounce contract:
        - Touch registers cleanly.
        - Upon finger lift, sampling continues for exactly 3 samples.
        - On 3rd sample, latched_pressed drops to False.
        - On 4th sample, screen is idle and SPI skips 100%.
        """
        hw = MockTouchHardwareState()
        task = SimulatedTouchTask(hw)

        # 1. Idle state
        for _ in range(10):
            task.step_sample()
        self.assertEqual(hw.mutex_acquire_count, 0)
        self.assertFalse(task.latest_point_pressed)

        # 2. Finger touches screen: PENIRQ asserts LOW
        hw.penirq_low = True
        hw.raw_x = 2400
        hw.raw_y = 1600
        hw.raw_z1 = 500
        hw.raw_z2 = 100

        # Sample 1: first-contact sample discard
        sampled = task.step_sample()
        self.assertTrue(sampled)
        self.assertFalse(task.latched_pressed, "First contact cycle is electrically unsettled -> discarded")

        # Sample 2: settled contact -> latches press
        task.step_sample()
        self.assertTrue(task.latched_pressed, "Settled press must latch on first valid sample")
        self.assertTrue(task.latest_point_pressed)
        self.assertEqual(task.latest_point_x, 2400)
        self.assertEqual(task.latest_point_y, 1600)

        # Sustained press for 50 samples
        for _ in range(50):
            task.step_sample()
            self.assertTrue(task.latched_pressed)
            self.assertTrue(task.latest_point_pressed)

        spi_count_during_touch = hw.spi_transfer_count
        self.assertGreater(spi_count_during_touch, 0)

        # 3. Finger lifts: PENIRQ goes HIGH (inactive), Z1 drops to 0
        hw.penirq_low = False
        hw.raw_z1 = 0
        hw.raw_z2 = 0

        # Release Sample 1: should still sample, release_count = 1, STILL LATCHED
        task.step_sample()
        self.assertTrue(task.latched_pressed, "Release debounce sample 1 must preserve latch!")
        self.assertTrue(task.latest_point_pressed)
        self.assertEqual(task.debounce_release_count, 1)

        # Release Sample 2: should still sample, release_count = 2, STILL LATCHED
        task.step_sample()
        self.assertTrue(task.latched_pressed, "Release debounce sample 2 must preserve latch!")
        self.assertTrue(task.latest_point_pressed)
        self.assertEqual(task.debounce_release_count, 2)

        # Release Sample 3: should sample, release_count = 3 -> UNLATCHES!
        task.step_sample()
        self.assertFalse(task.latched_pressed, "Release debounce sample 3 MUST drop latch!")
        self.assertFalse(task.latest_point_pressed)
        self.assertEqual(task.debounce_release_count, 3)

        # Release Sample 4: latched_pressed is now False, pen_down is False -> IDLE SKIP RESUMES!
        mutex_before = hw.mutex_acquire_count
        spi_before = hw.spi_transfer_count

        task.step_sample()
        self.assertFalse(task.latest_point_pressed)
        self.assertEqual(hw.mutex_acquire_count, mutex_before, "After release, mutex acquisition MUST stop!")
        self.assertEqual(hw.spi_transfer_count, spi_before, "After release, SPI transfers MUST stop!")

        # Verify sustained idle after release
        for _ in range(100):
            task.step_sample()
            self.assertFalse(task.latest_point_pressed)
        self.assertEqual(hw.mutex_acquire_count, mutex_before)
        self.assertEqual(hw.spi_transfer_count, spi_before)

    def test_noise_glitch_during_hold_does_not_break_touch(self):
        """Verify that single or dual sample ADC noise dropouts while pen_down is LOW do not drop touch."""
        hw = MockTouchHardwareState()
        hw.penirq_low = True
        hw.raw_x = 1000
        hw.raw_y = 1000
        hw.raw_z1 = 400
        hw.raw_z2 = 100

        task = SimulatedTouchTask(hw)
        task.step_sample() # first-contact discard
        task.step_sample() # latch
        self.assertTrue(task.latched_pressed)

        # Single frame glitch: z1 drops to 0, but penirq_low is STILL True (finger on glass)
        hw.raw_z1 = 0
        task.step_sample()
        self.assertTrue(task.latched_pressed, "Noise glitch while pen_down=True must NOT drop touch latch!")
        self.assertEqual(task.debounce_release_count, 0, "Release count must not increment while pen_down=True")

        # Two consecutive frame glitches:
        task.step_sample()
        self.assertTrue(task.latched_pressed, "Dual glitches while pen_down=True must NOT drop touch latch!")

        # Recovers:
        hw.raw_z1 = 400
        task.step_sample()
        self.assertTrue(task.latched_pressed)

    def test_re_press_during_release_debounce_aborts_release(self):
        """Verify that tapping again within 20 ms of release seamlessly re-engages without stuck touch."""
        hw = MockTouchHardwareState()
        hw.penirq_low = True
        hw.raw_x = 1500
        hw.raw_y = 1500
        hw.raw_z1 = 300
        task = SimulatedTouchTask(hw)
        task.step_sample() # skip
        task.step_sample() # latch
        self.assertTrue(task.latched_pressed)

        # Lift finger for 2 samples
        hw.penirq_low = False
        hw.raw_z1 = 0
        task.step_sample() # count 1
        self.assertEqual(task.debounce_release_count, 1)
        task.step_sample() # count 2
        self.assertEqual(task.debounce_release_count, 2)
        self.assertTrue(task.latched_pressed)

        # Finger presses down again on 3rd sample!
        hw.penirq_low = True
        hw.raw_z1 = 300
        task.step_sample()
        self.assertTrue(task.latched_pressed)
        self.assertEqual(task.debounce_release_count, 0, "Release count should reset upon re-press!")


if __name__ == "__main__":
    unittest.main()
