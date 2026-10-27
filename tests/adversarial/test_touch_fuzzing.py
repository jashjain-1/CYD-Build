"""Adversarial Touch Sampling, Jitter Fuzzing & Debounce State Machine Suite.

Fuzzes and stress-tests:
1. PENIRQ (GPIO 36 active-LOW) gating & first-contact sample discard.
2. Probe-only sample isolation (every 5 cycles when pen is not down).
3. 2-sample consecutive press confirmation against contact bounce and transient spikes.
4. 3-sample consecutive release confirmation against optical/mechanical dropouts.
5. Saturated read rejection (raw Z1 > 40, Z = 4095 + Z1 - Z2 > 180, raw in [80, 4050]).
6. Fault trigger at 25 consecutive saturated rejects.
"""

import os
import random
import sys

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

from tests.adversarial.test_adversarial_touch_arbitration import (
    TouchDebounceStateMachine,
    validate_touch_sample,
)


def test_first_contact_sample_discard():
    """Verify first sample immediately following PENIRQ assertion is electrically discarded."""
    valid, _rejects, presses, _faulted = validate_touch_sample(
        raw_x=1000, raw_y=1000, raw_z1=500, raw_z2=200,
        pen_down=True, latched_pressed=False, first_contact_skip=True
    )
    assert valid is False, "First contact sample must be discarded"
    assert presses == 0


def test_probe_only_sample_isolation():
    """Verify probe-only samples keep ADC warm but never latch coordinates or press."""
    valid, rejects, _presses, _faulted = validate_touch_sample(
        raw_x=1000, raw_y=1000, raw_z1=500, raw_z2=200,
        pen_down=False, latched_pressed=False, probe_only=True
    )
    assert valid is False, "Probe-only sample must never validate as press"
    assert rejects == 0, "Probe-only must not increment saturated rejects"


def test_two_sample_press_confirmation():
    """Verify 2 consecutive valid samples required to latch pressed = True."""
    sm = TouchDebounceStateMachine()
    assert sm.latched_pressed is False

    # Frame 1: Valid sample -> still not latched
    sm.update(valid_sample=True, raw_x=1200, raw_y=1400)
    assert sm.latched_pressed is False

    # Frame 2: Second valid sample -> latched pressed = True
    sm.update(valid_sample=True, raw_x=1200, raw_y=1400)
    assert sm.latched_pressed is True
    assert sm.latest_raw_x == 1200


def test_single_frame_spike_rejected_by_debounce():
    """Verify single transient noise sample (bounce) does not trigger press."""
    sm = TouchDebounceStateMachine()

    # Frame 1: Noise spike
    sm.update(valid_sample=True, raw_x=500, raw_y=500)
    assert sm.latched_pressed is False

    # Frame 2: Back to invalid (no touch)
    sm.update(valid_sample=False)
    assert sm.latched_pressed is False


def test_three_sample_release_confirmation():
    """Verify 3 consecutive invalid samples required to release a latched press."""
    sm = TouchDebounceStateMachine()

    # Establish press (2 valid frames)
    sm.update(valid_sample=True, raw_x=1000, raw_y=1000)
    sm.update(valid_sample=True, raw_x=1000, raw_y=1000)
    assert sm.latched_pressed is True

    # Frame 1 invalid (dropout) -> still latched pressed!
    sm.update(valid_sample=False)
    assert sm.latched_pressed is True

    # Frame 2 invalid -> still latched pressed!
    sm.update(valid_sample=False)
    assert sm.latched_pressed is True

    # Frame 3 invalid -> released!
    sm.update(valid_sample=False)
    assert sm.latched_pressed is False


def test_single_frame_dropout_tolerance():
    """Verify single frame dropout does not drop active press during continuous touch."""
    sm = TouchDebounceStateMachine()

    # Establish press
    sm.update(valid_sample=True, raw_x=1000, raw_y=1000)
    sm.update(valid_sample=True, raw_x=1000, raw_y=1000)
    assert sm.latched_pressed is True

    # Single-frame dropout
    sm.update(valid_sample=False)
    assert sm.latched_pressed is True

    # Touch recovers on next frame
    sm.update(valid_sample=True, raw_x=1005, raw_y=1005)
    assert sm.latched_pressed is True


def test_saturated_read_accumulation_trips_fault():
    """Verify saturated reads (Z <= 180 or raw_z1 <= 40) accumulate and trip fault at 25."""
    rejects = 0
    faulted = False

    for i in range(24):
        valid, rejects, _presses, faulted = validate_touch_sample(
            raw_x=1000, raw_y=1000, raw_z1=20, raw_z2=4000,  # Invalid Z1 <= 40
            saturated_rejects=rejects, pen_down=True
        )
        assert valid is False
        assert faulted is False

    assert rejects == 24

    # 25th saturated reject must trip fault
    valid, rejects, _presses, faulted = validate_touch_sample(
        raw_x=1000, raw_y=1000, raw_z1=20, raw_z2=4000,
        saturated_rejects=rejects, pen_down=True
    )
    assert rejects == 25
    assert faulted is True


def test_arbitrary_fuzz_stream_stability():
    """Fuzz debounce engine with 10,000 randomized state transitions."""
    random.seed(42)
    sm = TouchDebounceStateMachine()

    for _ in range(10000):
        sample = random.choice([True, False])
        rx = random.randint(50, 4000)
        ry = random.randint(50, 4000)
        state = sm.update(valid_sample=sample, raw_x=rx, raw_y=ry)
        assert isinstance(state, bool)
        assert 0 <= sm.debounce_press_count <= 2
        assert 0 <= sm.debounce_release_count <= 3
