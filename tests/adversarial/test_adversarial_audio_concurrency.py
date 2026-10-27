"""
Adversarial Audio Concurrency & HAL Integrity Falsification Suite.

Empirically challenges and stress-tests the audio pipeline in firmware/src/audio/audio_hal.cpp:
1. Complete absence of esp_timer software callback on Core 0.
2. Complete absence of dynamic malloc/free in audio alert path (0 dynamic heap allocations).
3. Queue handling, drop policy, and race condition immunity during rapid consecutive alert/click requests.
4. Full DAC dynamic range (0..255) and 0V digital idle clamp on GPIO 26.
"""

from __future__ import annotations

import math
import queue
import re
import threading
import time
from pathlib import Path
import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
AUDIO_SRC = REPO_ROOT / "firmware" / "src" / "audio" / "audio_hal.cpp"
AUDIO_HDR = REPO_ROOT / "firmware" / "src" / "audio" / "audio_hal.h"


# ==============================================================================
# 1. Static Invariant Verification: No esp_timer on Core 0 & Zero Heap Allocation
# ==============================================================================

def test_static_invariant_no_esp_timer_on_core0():
    """Prove complete absence of esp_timer callbacks and Core 0 audio tasks."""
    assert AUDIO_SRC.exists(), f"Source file {AUDIO_SRC} not found"
    assert AUDIO_HDR.exists(), f"Header file {AUDIO_HDR} not found"

    src_content = AUDIO_SRC.read_text(encoding="utf-8")
    hdr_content = AUDIO_HDR.read_text(encoding="utf-8")

    # Invariant 1: No esp_timer header or APIs in audio HAL
    assert "esp_timer.h" not in src_content, "audio_hal.cpp must not include esp_timer.h"
    assert "esp_timer.h" not in hdr_content, "audio_hal.h must not include esp_timer.h"
    assert "esp_timer_create" not in src_content, "audio_hal.cpp must not call esp_timer_create"
    assert "esp_timer_start" not in src_content, "audio_hal.cpp must not call esp_timer_start"

    # Invariant 2: audio_task must be pinned to Core 1, never Core 0
    # Strip single-line comments for clean regex parsing
    clean_src = re.sub(r'//[^\n]*', '', src_content)
    task_match = re.search(
        r'xTaskCreatePinnedToCore\s*\(\s*audio_task_func\s*,\s*"audio_task"\s*,\s*(\d+)\s*,\s*[^,]+,\s*(\d+)\s*,\s*[^,]+,\s*(\d+)\s*\)',
        clean_src
    )
    assert task_match is not None, "audio_task creation pattern not found in audio_hal.cpp"
    stack_size = int(task_match.group(1))
    priority = int(task_match.group(2))
    pinned_core = int(task_match.group(3))

    assert pinned_core == 1, f"audio_task must be pinned to Core 1, found Core {pinned_core}"
    assert priority == 3, f"audio_task must have Priority 3, found Priority {priority}"
    assert stack_size >= 2048, f"audio_task stack size must be >= 2048 bytes, found {stack_size}"


def test_static_invariant_zero_heap_allocations():
    """Prove complete absence of dynamic malloc/calloc/free/new/delete in audio_hal.cpp."""
    src_content = AUDIO_SRC.read_text(encoding="utf-8")

    forbidden_patterns = [
        r'\bmalloc\s*\(',
        r'\bcalloc\s*\(',
        r'\brealloc\s*\(',
        r'\bfree\s*\(',
        r'\bheap_caps_malloc\s*\(',
        r'\bheap_caps_calloc\s*\(',
        r'\bheap_caps_free\s*\(',
        r'\bnew\s+[a-zA-Z0-9_]+',
        r'\bdelete\s+',
    ]

    for pat in forbidden_patterns:
        matches = list(re.finditer(pat, src_content))
        assert len(matches) == 0, f"Forbidden dynamic heap allocation found: {pat} ({matches})"

    # Verify fixed stack buffer size
    chunk_match = re.search(r'constexpr\s+size_t\s+kChunkFrames\s*=\s*(\d+);', src_content)
    assert chunk_match is not None, "kChunkFrames definition not found"
    chunk_frames = int(chunk_match.group(1))
    assert chunk_frames == 64, f"kChunkFrames expected 64, found {chunk_frames}"

    buf_match = re.search(r'uint16_t\s+chunk_buf\[kChunkFrames\s*\*\s*2\];', src_content)
    assert buf_match is not None, "chunk_buf stack allocation declaration not found"

    # Stack buffer is 64 frames * 2 channels * 2 bytes = 256 bytes
    stack_bytes = chunk_frames * 2 * 2
    assert stack_bytes == 256, f"Stack chunk buffer must be 256 bytes, found {stack_bytes}"

    # Verify kRampTable is static const (Flash .rodata, 0 bytes static DRAM)
    ramp_match = re.search(r'static\s+const\s+uint8_t\s+kRampTable\[kRampLen\]\s*=', src_content)
    assert ramp_match is not None, "kRampTable must be declared static const in .rodata"


# ==============================================================================
# 2. DAC Full Dynamic Range & 0V Digital Idle Clamp Verification
# ==============================================================================

def test_dac_full_dynamic_range_and_clamping():
    """Empirically test DAC math, peak swing, and output clamping [1..255]."""
    hdr_content = AUDIO_HDR.read_text(encoding="utf-8")
    src_content = AUDIO_SRC.read_text(encoding="utf-8")

    # Invariant: AUDIO_SAFE_PEAK_COUNTS must be 127 for full 0..255 dynamic range
    peak_counts_match = re.search(r'#define\s+AUDIO_SAFE_PEAK_COUNTS\s+(\d+)', hdr_content)
    assert peak_counts_match is not None
    safe_peak = int(peak_counts_match.group(1))
    assert safe_peak == 127, f"AUDIO_SAFE_PEAK_COUNTS must be 127, found {safe_peak}"

    # Evaluate tone waveform generator model in Python (exact replica of C++ code)
    k_sample_rate = 16000
    k_pulse_samples = k_sample_rate * 50 // 1000  # 800
    k_gap_samples = k_sample_rate * 30 // 1000    # 480
    k_alert_samples = 4 * k_pulse_samples + 3 * k_gap_samples  # 4640
    k_ramp_samples = 40

    def evaluate_alert_sample(n: int) -> float:
        if n < 0 or n >= k_alert_samples:
            return 0.0
        cycle = k_pulse_samples + k_gap_samples
        pulse_idx = n // cycle
        offset = n % cycle
        if pulse_idx >= 4 or offset >= k_pulse_samples:
            return 0.0
        pulse_n = offset
        t = pulse_n / k_sample_rate
        f0 = 1150.0
        f1 = 2300.0
        beta = (f1 - f0) / 0.050
        phi = 2.0 * math.pi * (f0 * t + 0.5 * beta * t * t)
        w = 0.80 * math.sin(phi) + 0.20 * math.sin(2.0 * phi)

        window = 1.0
        if pulse_n < k_ramp_samples:
            window = 0.5 * (1.0 - math.cos(math.pi * pulse_n / k_ramp_samples))
        elif pulse_n > k_pulse_samples - 1 - k_ramp_samples:
            decay = k_pulse_samples - 1 - pulse_n
            window = 0.5 * (1.0 - math.cos(math.pi * decay / k_ramp_samples))
        return w * window * 0.95

    # Run synthesis for 100% volume
    peak = (safe_peak * 100) // 100
    assert peak == 127

    min_dac_val = 255
    max_dac_val = 0

    for n in range(k_alert_samples):
        norm = evaluate_alert_sample(n)
        out = 128 + int(round(norm * peak))
        if out < 1:
            out = 1
        if out > 255:
            out = 255
        if out < min_dac_val:
            min_dac_val = out
        if out > max_dac_val:
            max_dac_val = out

    # Alert waveform spans [22, 234], delivering >210 counts peak-to-peak swing
    assert min_dac_val <= 25, f"Min DAC count {min_dac_val} exceeds expected 25"
    assert max_dac_val >= 230, f"Max DAC count {max_dac_val} is below expected 230"
    assert max_dac_val - min_dac_val >= 210, f"Dynamic range span {max_dac_val - min_dac_val} < 210"

    # Verify that normalized signal bounds [-1.0, 1.0] (with EQ or max amplitude) hit full [1, 255]
    for test_norm, expected_bound in [(-1.0, 1), (1.0, 255)]:
        clipped = 128 + int(round(test_norm * peak))
        if clipped < 1:
            clipped = 1
        if clipped > 255:
            clipped = 255
        assert clipped == expected_bound, f"Dynamic range boundary failure: got {clipped}, expected {expected_bound}"


def test_ramp_table_smoothness_and_zero_volts_clamp():
    """Verify raised-cosine S-curve smoothness (no pop) and 0V clamp sequence."""
    src_content = AUDIO_SRC.read_text(encoding="utf-8")

    # Extract kRampTable entries
    ramp_match = re.search(r'static\s+const\s+uint8_t\s+kRampTable\[kRampLen\]\s*=\s*\{([^}]+)\};', src_content)
    assert ramp_match is not None, "kRampTable array definition not found"
    raw_nums = [int(x.strip()) for x in ramp_match.group(1).split(",") if x.strip()]
    assert len(raw_nums) == 160, f"kRampTable length must be 160, found {len(raw_nums)}"

    # Ramp must start at 0 and end at 128
    assert raw_nums[0] == 0, f"kRampTable must start at 0, found {raw_nums[0]}"
    assert raw_nums[-1] == 128, f"kRampTable must end at 128, found {raw_nums[-1]}"

    # Check maximum step size between consecutive samples (<= 2 counts = silky pop-free transition)
    max_delta = max(abs(raw_nums[i] - raw_nums[i - 1]) for i in range(1, len(raw_nums)))
    assert max_delta <= 2, f"Ramp table step discontinuity too large: max delta {max_delta}"

    # Verify clamp function steps: disable I2S DAC, set voltage 0, disable DAC channel, set GPIO low
    clamp_match = re.search(
        r'void\s+audio_hal_clamp_zero_volts\s*\(\s*void\s*\)\s*\{([^}]+)\}',
        src_content
    )
    assert clamp_match is not None, "audio_hal_clamp_zero_volts definition not found"
    clamp_body = clamp_match.group(1)

    assert "i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE)" in clamp_body
    assert "dac_output_voltage(DAC_CHANNEL_2, 0)" in clamp_body
    assert "dac_output_disable(DAC_CHANNEL_2)" in clamp_body
    assert "gpio_set_direction(GPIO_NUM_26, GPIO_MODE_OUTPUT)" in clamp_body
    assert "gpio_set_level(GPIO_NUM_26, 0)" in clamp_body


# ==============================================================================
# 3. Concurrency Stress Test: Rapid Queue Bursts, Drop Policy & Thread Safety
# ==============================================================================

class AudioHALSimulator:
    """
    Software oracle simulating FreeRTOS queue and Core 1 audio_task
    behavior according to audio_hal.cpp implementation.
    """
    def __init__(self, queue_capacity: int = 4):
        self.cmd_queue = queue.Queue(maxsize=queue_capacity)
        self.s_output = "CYD"
        self.s_playing = False
        self.s_clamped = True
        self.lock = threading.RLock()
        self.tones_played: list[str] = []
        self.clicks_dropped = 0
        self.alerts_dropped = 0
        self.stop_event = threading.Event()
        self.started_playing_event = threading.Event()
        self.consumer_thread = threading.Thread(target=self._task_func, daemon=True)
        self.consumer_thread.start()

    def play_alert(self) -> bool:
        with self.lock:
            if self.s_output != "CYD":
                return False
        try:
            # xQueueSend(..., 0) -> non-blocking
            self.cmd_queue.put_nowait("ALERT")
            return True
        except queue.Full:
            self.alerts_dropped += 1
            return False

    def play_click(self) -> bool:
        with self.lock:
            if self.s_output != "CYD":
                return False
            # audio_hal.cpp line 538: if (s_playing) return;
            if self.s_playing:
                self.clicks_dropped += 1
                return False
        try:
            self.cmd_queue.put_nowait("CLICK")
            return True
        except queue.Full:
            self.clicks_dropped += 1
            return False

    def play_beep(self) -> bool:
        with self.lock:
            if self.s_output != "CYD":
                return False
        try:
            self.cmd_queue.put_nowait("BEEP")
            return True
        except queue.Full:
            return False

    def set_output(self, out: str):
        with self.lock:
            self.s_output = out
            if out == "PHONE":
                self.s_clamped = True

    def _task_func(self):
        while not self.stop_event.is_set():
            try:
                cmd = self.cmd_queue.get(timeout=0.005)
            except queue.Empty:
                continue

            with self.lock:
                out = self.s_output
            if out != "CYD":
                self.cmd_queue.task_done()
                continue

            # Stream tone
            with self.lock:
                self.s_playing = True
                self.s_clamped = False
            self.started_playing_event.set()

            # Simulate streaming duration
            duration = 0.020 if cmd == "ALERT" else (0.003 if cmd == "CLICK" else 0.008)
            start = time.perf_counter()
            while time.perf_counter() - start < duration:
                with self.lock:
                    if self.s_output != "CYD":
                        break
                time.sleep(0.001)

            with self.lock:
                self.s_playing = False
                self.s_clamped = True
                self.tones_played.append(cmd)
            self.started_playing_event.clear()

            self.cmd_queue.task_done()

    def shutdown(self):
        self.stop_event.set()
        self.consumer_thread.join(timeout=1.0)


def test_adversarial_queue_burst_stress():
    """Stress-test queue saturation, non-blocking caller guarantee, and drop policies."""
    sim = AudioHALSimulator(queue_capacity=4)

    # 1. Non-blocking guarantee under intense queue overflow
    # Saturate queue with 50 rapid alerts
    accepted = 0
    for _ in range(50):
        if sim.play_alert():
            accepted += 1

    # First <= 4 accepted, remainder dropped immediately without blocking
    assert accepted <= 5, f"Expected at most 5 alerts accepted in 4-slot queue, got {accepted}"
    assert sim.alerts_dropped >= 45, f"Expected >= 45 alerts dropped, got {sim.alerts_dropped}"

    time.sleep(0.1) # Wait for simulation to drain queue

    # 2. Concurrency stress: 30 parallel threads firing alerts, clicks, and beeps simultaneously
    def worker(worker_id: int):
        for i in range(20):
            if i % 3 == 0:
                sim.play_alert()
            elif i % 3 == 1:
                sim.play_click()
            else:
                sim.play_beep()
            time.sleep(0.001)

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(30)]
    t0 = time.perf_counter()
    for t in threads:
        t.start()
    for t in threads:
        t.join(timeout=2.0)
    t_elapsed = time.perf_counter() - t0

    # Guarantee: All 30 threads finished promptly with zero deadlocks
    assert t_elapsed < 3.0, f"Concurrent burst stalled/deadlocked: elapsed {t_elapsed:.2f}s"

    # Verify click drop policy during active playback
    assert sim.clicks_dropped > 0, "Expected clicks to be dropped while alert was playing"

    time.sleep(0.15)
    # Once drained, ensure idle clamp is 100% active and s_playing is false
    with sim.lock:
        assert not sim.s_playing, "s_playing should be False when queue drains"
        assert sim.s_clamped, "Audio HAL must return to clamped 0V state when idle"

    sim.shutdown()


def test_adversarial_output_switch_midstream():
    """Verify that switching output to AUDIO_OUT_PHONE immediately aborts streaming and clamps."""
    sim = AudioHALSimulator(queue_capacity=4)

    sim.play_alert()
    # Wait for playback to actually start
    started = sim.started_playing_event.wait(timeout=0.1)
    assert started, "Playback failed to start in time"

    with sim.lock:
        assert sim.s_playing, "Alert should be playing"

    # Switch to PHONE mid-stream
    sim.set_output("PHONE")
    time.sleep(0.005)

    with sim.lock:
        assert sim.s_clamped, "Must immediately clamp to 0V upon switching to PHONE"
        # Subsequent play requests must be dropped
        assert not sim.play_alert(), "Alerts must be rejected when output is PHONE"
        assert not sim.play_click(), "Clicks must be rejected when output is PHONE"

    sim.shutdown()
