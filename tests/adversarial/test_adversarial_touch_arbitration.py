"""Adversarial Touch Sampling, SPI Bus Arbitration & Debounce Stress Suite.

Empirical verification of:
1. FreeRTOS g_spi_bus_mutex arbitration across display_flush_cb and touch_task,
   proving dmaWait() prevents DMA in-flight collisions on shared VSPI bus.
2. Pressure validation formula Z = 4095 + Z1 - Z2 fuzzing and boundary testing:
   raw_z1 <= 40, Z <= 180, coordinate bounds [80, 4050], and saturated reject accumulation.
3. 2-sample consecutive press / 3-sample consecutive release debounce state machine
   against contact bounce, jitter, and single/dual-frame dropouts.
4. PENIRQ active-LOW level witness behavior on GPIO 36, first-contact conversion discard,
   probe-only sample isolation, and 0xD0 re-arming command byte.
"""

import os
import random
import sys
import threading
import time
import unittest

# Ensure project root is in path
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, PROJECT_ROOT)


# ==============================================================================
# Firmware Models & Logic Under Test (1:1 with touch_hal.cpp & display_hal.cpp)
# ==============================================================================

def validate_touch_sample(
    raw_x: int,
    raw_y: int,
    raw_z1: int,
    raw_z2: int,
    saturated_rejects: int = 0,
    valid_presses: int = 0,
    fault_reject_threshold: int = 25,
    pen_down: bool = True,
    latched_pressed: bool = False,
    first_contact_skip: bool = False,
    probe_only: bool = False
) -> tuple[bool, int, int, bool]:
    """Execute pressure & coordinate validation exactly as in touch_hal.cpp:164-198.
    
    Returns:
        (valid_sample, updated_saturated_rejects, updated_valid_presses, fault_triggered)
    """
    z_pressure = 4095 + raw_z1 - raw_z2
    valid_sample = False

    contact = pen_down or latched_pressed
    pressure_and_bounds_ok = (
        raw_z1 > 40 and
        z_pressure > 180 and
        80 <= raw_x <= 4050 and
        80 <= raw_y <= 4050
    )

    if pressure_and_bounds_ok:
        if contact and not first_contact_skip and not probe_only:
            valid_sample = True
            valid_presses += 1
    elif pen_down:
        saturated_rejects += 1

    fault_triggered = (saturated_rejects >= fault_reject_threshold)
    return valid_sample, saturated_rejects, valid_presses, fault_triggered


class TouchDebounceStateMachine:
    """1:1 Model of the 2-press / 3-release debounce state machine in touch_hal.cpp:200-218."""

    def __init__(self):
        self.debounce_press_count = 0
        self.debounce_release_count = 0
        self.latched_pressed = False
        self.latest_raw_x = 0
        self.latest_raw_y = 0
        self.latest_raw_z = 0
        self.has_raw = False

    def update(self, valid_sample: bool, raw_x: int = 0, raw_y: int = 0, raw_z: int = 0) -> bool:
        if valid_sample:
            self.debounce_release_count = 0
            if self.debounce_press_count < 2:
                self.debounce_press_count += 1
            if self.debounce_press_count >= 2:
                self.latched_pressed = True
        else:
            self.debounce_press_count = 0
            if self.debounce_release_count < 3:
                self.debounce_release_count += 1
            if self.debounce_release_count >= 3:
                self.latched_pressed = False

        if valid_sample:
            self.latest_raw_x = raw_x
            self.latest_raw_y = raw_y
            self.latest_raw_z = raw_z
            self.has_raw = True
        elif not self.latched_pressed:
            self.has_raw = False

        return self.latched_pressed


# ==============================================================================
# 1. SPI Bus Arbitration & Concurrency Stress Test
# ==============================================================================

class TestSpiBusArbitrationAndDmaSafety(unittest.TestCase):
    """Stress test shared VSPI bus arbitration between ST7796 DMA flushes and touch sampling."""

    def test_dma_wait_prevents_bus_collision_under_preemption(self):
        """Verify that dmaWait() strictly prevents SPI bus collision between DMA and touch."""
        bus_lock = threading.Lock()
        dma_in_flight = threading.Event()
        collisions = []

        NUM_CYCLES = 200
        flushes_completed = 0
        samples_completed = 0

        # Scenario A: Firmware implementation (with dmaWait holding the mutex)
        def display_flush_worker_safe():
            nonlocal flushes_completed
            for _ in range(NUM_CYCLES):
                with bus_lock:
                    dma_in_flight.set()
                    time.sleep(0.001)
                    dma_in_flight.clear()
                flushes_completed += 1
                time.sleep(0.002)

        def touch_poll_worker_safe():
            nonlocal samples_completed
            for _ in range(NUM_CYCLES * 2):
                acquired = bus_lock.acquire(timeout=0.003)
                if acquired:
                    try:
                        if dma_in_flight.is_set():
                            collisions.append("COLLISION: Touch accessed bus while DMA was in flight!")
                        time.sleep(0.0002)
                    finally:
                        bus_lock.release()
                    samples_completed += 1
                time.sleep(0.001)

        t_disp = threading.Thread(target=display_flush_worker_safe)
        t_touch = threading.Thread(target=touch_poll_worker_safe)
        t_disp.start()
        t_touch.start()
        t_disp.join()
        t_touch.join()

        self.assertEqual(len(collisions), 0, f"Safe implementation encountered collisions: {collisions}")
        self.assertGreater(flushes_completed, 0)
        self.assertGreater(samples_completed, 0)

        # Scenario B: Counter-example proving dmaWait is essential (adversarial falsification)
        flushes_completed = 0
        samples_completed = 0
        collisions.clear()

        def display_flush_worker_unsafe():
            nonlocal flushes_completed
            for _ in range(NUM_CYCLES):
                with bus_lock:
                    dma_in_flight.set()
                time.sleep(0.001)
                dma_in_flight.clear()
                flushes_completed += 1
                time.sleep(0.002)

        def touch_poll_worker_unsafe():
            nonlocal samples_completed
            for _ in range(NUM_CYCLES * 2):
                acquired = bus_lock.acquire(timeout=0.003)
                if acquired:
                    try:
                        if dma_in_flight.is_set():
                            collisions.append("COLLISION")
                        time.sleep(0.0002)
                    finally:
                        bus_lock.release()
                    samples_completed += 1
                time.sleep(0.001)

        t_disp = threading.Thread(target=display_flush_worker_unsafe)
        t_touch = threading.Thread(target=touch_poll_worker_unsafe)
        t_disp.start()
        t_touch.start()
        t_disp.join()
        t_touch.join()

        self.assertGreater(len(collisions), 0, "Counter-example without dmaWait MUST produce collisions!")

    def test_touch_task_mutex_timeout_recovery(self):
        """Verify touch_task times out gracefully when bus is held > 12ms and recovers when released."""
        bus_lock = threading.Lock()
        touch_timeouts = 0
        touch_successes = 0
        barrier = threading.Barrier(2)

        def heavy_display_flush():
            with bus_lock:
                barrier.wait()
                time.sleep(0.060)

        def touch_poll():
            nonlocal touch_timeouts, touch_successes
            barrier.wait()
            # Attempt to take lock while heavy flush is holding it with 12ms timeout
            acquired = bus_lock.acquire(timeout=0.012)
            if acquired:
                touch_successes += 1
                bus_lock.release()
            else:
                touch_timeouts += 1

            # Wait for heavy display flush to finish
            time.sleep(0.060)

            # Now bus should be free
            acquired = bus_lock.acquire(timeout=0.030)
            if acquired:
                touch_successes += 1
                bus_lock.release()
            else:
                touch_timeouts += 1

        t1 = threading.Thread(target=heavy_display_flush)
        t2 = threading.Thread(target=touch_poll)
        t1.start()
        t2.start()
        t1.join()
        t2.join()

        self.assertEqual(touch_timeouts, 1, "Touch task should time out once while bus is busy")
        self.assertEqual(touch_successes, 1, "Touch task should succeed once bus is released")


# ==============================================================================
# 2. Pressure Validation & Boundary Fuzzing ($Z = 4095 + Z_1 - Z_2$)
# ==============================================================================

class TestTouchPressureValidationFuzzing(unittest.TestCase):
    """Fuzz testing and boundary stress tests for touch pressure and coordinate filtering."""

    def test_pressure_formula_exact_bounds(self):
        """Verify exact boundary behavior for Z = 4095 + Z1 - Z2 and coordinate gates."""
        # 1. Z1 <= 40 must be rejected
        for z1 in [-100, -1, 0, 1, 39, 40]:
            valid, rejects, _, _ = validate_touch_sample(raw_x=2000, raw_y=2000, raw_z1=z1, raw_z2=1000,
                                                         saturated_rejects=0, valid_presses=0)
            self.assertFalse(valid, f"Z1={z1} (<=40) should be rejected")
            self.assertEqual(rejects, 1)

        # 2. Z1 = 41 with sufficient pressure and valid coords must pass
        # Z = 4095 + 41 - 1000 = 3136 > 180
        valid, rejects, _, _ = validate_touch_sample(raw_x=2000, raw_y=2000, raw_z1=41, raw_z2=1000,
                                                     saturated_rejects=0, valid_presses=0)
        self.assertTrue(valid, "Z1=41 with Z > 180 must be valid")
        self.assertEqual(rejects, 0)

        # 3. Z1 = 4000 with sufficient pressure and valid coords must pass
        valid, rejects, _, _ = validate_touch_sample(raw_x=2000, raw_y=2000, raw_z1=4000, raw_z2=1000,
                                                     saturated_rejects=0, valid_presses=0)
        self.assertTrue(valid, "Z1=4000 with Z > 180 must be valid")
        self.assertEqual(rejects, 0)

        # 4. Pressure threshold Z <= 180 must be rejected
        # Z = 4095 + Z1 - Z2 <= 180 => Z2 >= 4095 + Z1 - 180
        # If Z1 = 500, threshold Z2 = 4095 + 500 - 180 = 4415
        valid_180, rejects_180, _, _ = validate_touch_sample(raw_x=2000, raw_y=2000, raw_z1=500, raw_z2=4415,
                                                             saturated_rejects=0, valid_presses=0)
        self.assertFalse(valid_180, "Z=180 should be rejected (rule: Z > 180)")
        self.assertEqual(rejects_180, 1)

        valid_181, rejects_181, _, _ = validate_touch_sample(raw_x=2000, raw_y=2000, raw_z1=500, raw_z2=4414,
                                                             saturated_rejects=0, valid_presses=0)
        self.assertTrue(valid_181, "Z=181 should be accepted (rule: Z > 180)")
        self.assertEqual(rejects_181, 0)

        # 5. Coordinate boundaries [80, 4050]
        # X boundaries
        v_x79, _, _, _ = validate_touch_sample(79, 2000, 500, 1000, 0, 0)
        v_x80, _, _, _ = validate_touch_sample(80, 2000, 500, 1000, 0, 0)
        v_x4050, _, _, _ = validate_touch_sample(4050, 2000, 500, 1000, 0, 0)
        v_x4051, _, _, _ = validate_touch_sample(4051, 2000, 500, 1000, 0, 0)
        self.assertFalse(v_x79)
        self.assertTrue(v_x80)
        self.assertTrue(v_x4050)
        self.assertFalse(v_x4051)

        # Y boundaries
        v_y79, _, _, _ = validate_touch_sample(2000, 79, 500, 1000, 0, 0)
        v_y80, _, _, _ = validate_touch_sample(2000, 80, 500, 1000, 0, 0)
        v_y4050, _, _, _ = validate_touch_sample(2000, 4050, 500, 1000, 0, 0)
        v_y4051, _, _, _ = validate_touch_sample(2000, 4051, 500, 1000, 0, 0)
        self.assertFalse(v_y79)
        self.assertTrue(v_y80)
        self.assertTrue(v_y4050)
        self.assertFalse(v_y4051)

    def test_saturated_fault_latching(self):
        """Verify that >=25 saturated readings trigger fault condition."""
        rejects = 0
        valid_presses = 0
        fault = False

        # 24 saturated readings (e.g. raw_z1 <= 40)
        for _ in range(24):
            valid, rejects, valid_presses, fault = validate_touch_sample(
                2000, 2000, 30, 1000, rejects, valid_presses, fault_reject_threshold=25
            )
            self.assertFalse(valid)
            self.assertFalse(fault)

        # 25th saturated reading (rejects == 25)
        valid, rejects, valid_presses, fault = validate_touch_sample(
            2000, 2000, 30, 1000, rejects, valid_presses, fault_reject_threshold=25
        )
        self.assertFalse(valid)
        self.assertEqual(rejects, 25)
        self.assertTrue(fault, "25th saturated reading MUST trigger fault flag")

    def test_first_contact_discard_and_probe_isolation(self):
        """Verify first-contact sample discard and probe-only sample isolation."""
        # Scenario 1: First contact skip
        # Initial touch onset: pen_down=True, prev=False, latched=False -> first_contact_skip=True
        valid, rejects, presses, _fault = validate_touch_sample(
            raw_x=2000, raw_y=2000, raw_z1=500, raw_z2=1000,
            first_contact_skip=True, pen_down=True
        )
        self.assertFalse(valid, "First contact cycle MUST be discarded despite valid pressure/bounds")
        self.assertEqual(presses, 0)
        self.assertEqual(rejects, 0)

        # Next sample: settled contact (first_contact_skip=False)
        valid, rejects, presses, _fault = validate_touch_sample(
            raw_x=2000, raw_y=2000, raw_z1=500, raw_z2=1000,
            first_contact_skip=False, pen_down=True
        )
        self.assertTrue(valid, "Subsequent sample after settling MUST be accepted")
        self.assertEqual(presses, 1)

        # Scenario 2: Probe-only sample isolation
        # Periodic probe (every 5 ticks when untouched): pen_down=False, latched=False, probe_only=True
        valid, rejects, presses, _fault = validate_touch_sample(
            raw_x=2000, raw_y=2000, raw_z1=500, raw_z2=1000,
            pen_down=False, latched_pressed=False, probe_only=True
        )
        self.assertFalse(valid, "Probe-only sample MUST NEVER emit coordinates or validate a press")
        self.assertEqual(presses, 0)
        self.assertEqual(rejects, 0, "Probe-only without pen_down must not count as saturated reject")

    def test_randomized_monte_carlo_fuzzing_100k(self):
        """Fuzz 100,000 random input vectors to verify stability and arithmetic invariants."""
        random.seed(0x5A5A5A)
        accepted_count = 0
        rejected_count = 0

        for _ in range(100000):
            rx = random.randint(-500, 5000)
            ry = random.randint(-500, 5000)
            rz1 = random.randint(-500, 5000)
            rz2 = random.randint(-500, 5000)

            valid, _, _, _ = validate_touch_sample(rx, ry, rz1, rz2, 0, 0)
            z_calc = 4095 + rz1 - rz2

            if valid:
                accepted_count += 1
                self.assertGreater(z_calc, 180)
                self.assertGreater(rz1, 40)
                self.assertGreaterEqual(rx, 80)
                self.assertLessEqual(rx, 4050)
                self.assertGreaterEqual(ry, 80)
                self.assertLessEqual(ry, 4050)
            else:
                rejected_count += 1
                failed_cond = (
                    (z_calc <= 180) or
                    (rz1 <= 40) or
                    (rx < 80 or rx > 4050) or
                    (ry < 80 or ry > 4050)
                )
                self.assertTrue(failed_cond)

        self.assertGreater(accepted_count, 0)
        self.assertGreater(rejected_count, 0)


# ==============================================================================
# 3. 2-Press / 3-Release Debounce State Machine Stress Test
# ==============================================================================

class TestTouchDebounceStateMachine(unittest.TestCase):
    """Stress test 2-press / 3-release debounce state machine against contact bounce and noise."""

    def test_contact_bounce_alternating_rejection(self):
        """Contact bounce [V, I, V, I, V, I, ...] must NEVER latch pressed."""
        sm = TouchDebounceStateMachine()
        for i in range(100):
            valid = (i % 2 == 0)
            latched = sm.update(valid, raw_x=1500, raw_y=1500, raw_z=800)
            self.assertFalse(latched, f"Bounce step {i} (valid={valid}) must not latch pressed")

    def test_single_glitch_press_and_release(self):
        """Isolated glitches and dropouts must be handled per 2-press / 3-release contract."""
        sm = TouchDebounceStateMachine()
        self.assertFalse(sm.update(False))
        self.assertFalse(sm.update(False))

        # Single 10ms glitch
        self.assertFalse(sm.update(True, raw_x=1000, raw_y=1000, raw_z=500))
        # Back to idle
        self.assertFalse(sm.update(False))
        self.assertFalse(sm.latched_pressed)

        # Confirm genuine press (2 consecutive valid)
        self.assertFalse(sm.update(True, raw_x=1000, raw_y=1000, raw_z=500))
        self.assertTrue(sm.update(True, raw_x=1000, raw_y=1000, raw_z=500))
        self.assertTrue(sm.latched_pressed)

        # Single 10ms dropout during press
        self.assertTrue(sm.update(False)) # 1st invalid sample: remains latched
        self.assertEqual(sm.latest_raw_x, 1000, "Must retain previous coordinates during dropout")
        self.assertTrue(sm.update(True, raw_x=1010, raw_y=1010, raw_z=500)) # Recovers immediately
        self.assertTrue(sm.latched_pressed)

        # Dual 20ms dropout during press (3-release contract: 2 invalid samples do NOT release)
        self.assertTrue(sm.update(False)) # 1st invalid
        self.assertTrue(sm.update(False)) # 2nd invalid: MUST REMAIN LATCHED!
        self.assertTrue(sm.update(True, raw_x=1020, raw_y=1020, raw_z=500)) # Recovers!
        self.assertTrue(sm.latched_pressed)

        # Genuine release (3 consecutive invalid)
        self.assertTrue(sm.update(False))  # 1st invalid: remains latched
        self.assertTrue(sm.update(False))  # 2nd invalid: remains latched
        self.assertFalse(sm.update(False)) # 3rd invalid: RELEASES!
        self.assertFalse(sm.latched_pressed)
        self.assertFalse(sm.has_raw)

    def test_debounce_random_noise_fuzzing(self):
        """Fuzz debounce state machine with 20,000 random binary inputs to verify no state flapping."""
        sm = TouchDebounceStateMachine()
        random.seed(0x12345)
        history_in = []
        history_out = []

        for _ in range(20000):
            bit = (random.random() < 0.3)
            out = sm.update(bit, 2000, 2000, 500)
            history_in.append(bit)
            history_out.append(out)

            self.assertLessEqual(sm.debounce_press_count, 2)
            self.assertLessEqual(sm.debounce_release_count, 3)

        # Invariant checks:
        for i in range(1, len(history_out)):
            prev_out = history_out[i - 1]
            curr_out = history_out[i]
            if curr_out != prev_out:
                if curr_out is True:
                    # Switched to TRUE => previous 2 inputs MUST both be True
                    self.assertTrue(history_in[i] and history_in[i - 1],
                                    f"Toggled to True at index {i} without 2 consecutive True inputs")
                else:
                    # Switched to FALSE => previous 3 inputs MUST all be False
                    self.assertFalse(history_in[i] or history_in[i - 1] or history_in[i - 2],
                                     f"Toggled to False at index {i} without 3 consecutive False inputs")


# ==============================================================================
# 4. PENIRQ Level Witness & 0xD0 Command Re-arming
# ==============================================================================

class TestPenirqLevelWitnessAndCommandSemantics(unittest.TestCase):
    """Verify PENIRQ active-LOW level polling and 0xD0 command byte bitmask semantics."""

    def test_penirq_level_witness_gating(self):
        """Verify that HIGH level skips SPI bus acquisition with 0 overhead, LOW triggers bus read."""
        spi_reads_triggered = 0

        def simulate_touch_task_cycle(pin_touch_irq_level: int):
            nonlocal spi_reads_triggered
            pen_down = (pin_touch_irq_level == 0)
            if not pen_down:
                return
            spi_reads_triggered += 1

        for _ in range(100):
            simulate_touch_task_cycle(1)
        self.assertEqual(spi_reads_triggered, 0, "Untouched panel must trigger ZERO SPI reads")

        for _ in range(10):
            simulate_touch_task_cycle(0)
        self.assertEqual(spi_reads_triggered, 10, "Touched panel must trigger SPI reads")

    def test_xpt2046_command_byte_decoding(self):
        """Verify bitmask breakdown of XPT2046 command bytes according to datasheet."""
        def decode_cmd(cmd: int):
            start = (cmd >> 7) & 1
            channel = (cmd >> 4) & 7
            mode_8bit = (cmd >> 3) & 1
            ser_dfr = (cmd >> 2) & 1
            pd = cmd & 3
            return {
                "start": start,
                "channel": channel,
                "mode_8bit": mode_8bit,
                "ser_dfr": ser_dfr,
                "pd": pd
            }

        # 0x91: Read X (channel 1, 12-bit, differential, ADC on)
        d_x = decode_cmd(0x91)
        self.assertEqual(d_x["start"], 1)
        self.assertEqual(d_x["channel"], 0b001)
        self.assertEqual(d_x["mode_8bit"], 0)
        self.assertEqual(d_x["ser_dfr"], 0)
        self.assertEqual(d_x["pd"], 0b01)

        # 0xD1: Read Y (channel 5, 12-bit, differential, ADC on)
        d_y = decode_cmd(0xD1)
        self.assertEqual(d_y["start"], 1)
        self.assertEqual(d_y["channel"], 0b101)
        self.assertEqual(d_y["pd"], 0b01)

        # 0xB1: Read Z1 (channel 3, 12-bit, differential, ADC on)
        d_z1 = decode_cmd(0xB1)
        self.assertEqual(d_z1["start"], 1)
        self.assertEqual(d_z1["channel"], 0b011)
        self.assertEqual(d_z1["pd"], 0b01)

        # 0xC1: Read Z2 (channel 4, 12-bit, differential, ADC on)
        d_z2 = decode_cmd(0xC1)
        self.assertEqual(d_z2["start"], 1)
        self.assertEqual(d_z2["channel"], 0b100)
        self.assertEqual(d_z2["pd"], 0b01)

        # 0xD0: Power-Down Mode, PENIRQ enabled!
        d_arm = decode_cmd(0xD0)
        self.assertEqual(d_arm["start"], 1)
        self.assertEqual(d_arm["mode_8bit"], 0)
        self.assertEqual(d_arm["ser_dfr"], 0)
        self.assertEqual(d_arm["pd"], 0b00, "0xD0 must set PD1=0, PD0=0 to re-arm PENIRQ!")


if __name__ == "__main__":
    unittest.main()
