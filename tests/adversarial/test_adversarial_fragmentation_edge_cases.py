"""Adversarial Protocol Fragmentation & Flow Control Edge-Case Verification Suite.

Adversarially falsifies and stress-tests:
1. Aborted bulk frames (rapid track skips cleanly restarting reassembly without drops or leaks).
2. Control duplicate rejection (strict rejection of duplicate frag 0 on control lane).
3. Out-of-order and dropped fragments (gap skips, orphan fragments, intermediate duplicates).
4. Length bounds and buffer overflow resistance (implied pre-bounds attack, cumulative overflow).
5. Reassembly timeout (2.0s gap threshold and lane deactivation).
6. Concurrent bi-lane interleaving under MTU 23 and MTU 247 without cross-talk.
7. Flow control credit accounting & lane isolation (Lane 0 control reserved credit never starved).
"""

from __future__ import annotations

import os
import sys
import unittest

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "tools", "test-only", "fuzz"))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "tools", "test-only", "ble_sim"))

from fuzz_protocol_harness import (
    MAX_FRAME_SIZE,
    REASSEMBLY_TIMEOUT_S,
    FragmentMutator,
    Lane,
    StatefulReassemblerOracle,
)

from protocol import FlowController, LaneType


class TestAdversarialFragmentationEdgeCases(unittest.TestCase):

    def setUp(self):
        self.oracle = StatefulReassemblerOracle()

    # --------------------------------------------------------------------------
    # 1. Aborted Bulk Frames (Rapid Track Skips)
    # --------------------------------------------------------------------------
    def test_adv_aborted_bulk_frame_clean_restart(self):
        """Stress: A bulk frame aborted halfway by a rapid track skip must cleanly restart on frag 0."""
        # Frame A: 5 fragments on Bulk lane
        raw_a = b"TrackA_Chunk0_Chunk1_Chunk2_Chunk3_Chunk4"
        frags_a = FragmentMutator.slice_message(raw_a, lane=Lane.BULK, mtu=23)
        self.assertGreaterEqual(len(frags_a), 3)

        # Feed first 2 fragments of Frame A
        assembled, status = self.oracle.feed(frags_a[0], now_s=10.0)
        self.assertEqual(status, "IN_PROGRESS")
        self.assertIsNotNone(self.oracle.lanes[Lane.BULK])
        self.assertEqual(self.oracle.lanes[Lane.BULK]["next_idx"], 1)

        assembled, status = self.oracle.feed(frags_a[1], now_s=10.05)
        self.assertEqual(status, "IN_PROGRESS")
        self.assertEqual(self.oracle.lanes[Lane.BULK]["next_idx"], 2)
        bytes_before_abort = self.oracle.lanes[Lane.BULK]["total_bytes"]

        # Sudden Track Skip: Frame B starts with frag 0 before Frame A finishes
        raw_b = b"TrackB_NewArtworkDataPayload123456789"
        frags_b = FragmentMutator.slice_message(raw_b, lane=Lane.BULK, mtu=23)

        # Feeding frag 0 of Frame B must cleanly abort Frame A without error
        assembled, status = self.oracle.feed(frags_b[0], now_s=10.10)
        self.assertEqual(status, "IN_PROGRESS")
        self.assertIsNotNone(self.oracle.lanes[Lane.BULK])
        self.assertEqual(self.oracle.lanes[Lane.BULK]["next_idx"], 1)
        self.assertEqual(self.oracle.lanes[Lane.BULK]["total_frags"], len(frags_b))
        self.assertLess(self.oracle.lanes[Lane.BULK]["total_bytes"], bytes_before_abort)
        self.assertEqual(self.oracle.out_of_order_drops, 0, "Clean restart on Bulk must NOT drop or count error")

        # Complete Frame B
        for f in frags_b[1:]:
            assembled, status = self.oracle.feed(f, now_s=10.15)

        self.assertEqual(status, "COMPLETE")
        self.assertEqual(assembled, raw_b)
        self.assertIsNone(self.oracle.lanes[Lane.BULK])

    # --------------------------------------------------------------------------
    # 2. Control Lane: Duplicate Rejection vs Bulk Restart
    # --------------------------------------------------------------------------
    def test_adv_control_lane_rejects_duplicate_frag_0(self):
        """Stress: Control lane MUST NOT restart on identical duplicate frag 0; must reject as out-of-order."""
        c_frag0 = FragmentMutator.pack_canonical_fragment(
            lane=Lane.CONTROL, total_frags=3, frag_idx=0, credits=8, chunk=b"ControlPayload_0"
        )

        _assembled, status = self.oracle.feed(c_frag0, now_s=20.0)
        self.assertEqual(status, "IN_PROGRESS")
        self.assertIsNotNone(self.oracle.lanes[Lane.CONTROL])

        # Duplicate frag 0 arrives while control lane is expecting frag 1
        _assembled, status = self.oracle.feed(c_frag0, now_s=20.05)
        self.assertEqual(status, "ERROR_OUT_OF_ORDER")
        self.assertIsNone(self.oracle.lanes[Lane.CONTROL], "Control lane must be reset on duplicate frag 0")
        self.assertEqual(self.oracle.out_of_order_drops, 1)

    # --------------------------------------------------------------------------
    # 3. Out-of-Order Packets, Intermediate Duplicates & Gaps
    # --------------------------------------------------------------------------
    def test_adv_gap_skip_rejection(self):
        """Stress: Skipping an intermediate fragment (e.g. frag 0 then frag 2) triggers error and resets lane."""
        f0 = FragmentMutator.pack_canonical_fragment(Lane.BULK, total_frags=4, frag_idx=0, credits=8, chunk=b"F0")
        f2 = FragmentMutator.pack_canonical_fragment(Lane.BULK, total_frags=4, frag_idx=2, credits=8, chunk=b"F2")

        self.oracle.feed(f0, now_s=30.0)
        _assembled, status = self.oracle.feed(f2, now_s=30.05)
        self.assertEqual(status, "ERROR_OUT_OF_ORDER")
        self.assertIsNone(self.oracle.lanes[Lane.BULK])
        self.assertEqual(self.oracle.out_of_order_drops, 1)

    def test_adv_intermediate_duplicate_rejection(self):
        """Stress: Duplicate intermediate fragment (0 -> 1 -> 1) triggers error and resets lane."""
        f0 = FragmentMutator.pack_canonical_fragment(Lane.CONTROL, total_frags=3, frag_idx=0, credits=8, chunk=b"C0")
        f1 = FragmentMutator.pack_canonical_fragment(Lane.CONTROL, total_frags=3, frag_idx=1, credits=8, chunk=b"C1")

        self.oracle.feed(f0, now_s=40.0)
        self.oracle.feed(f1, now_s=40.05)
        _assembled, status = self.oracle.feed(f1, now_s=40.10) # duplicate f1!
        self.assertEqual(status, "ERROR_OUT_OF_ORDER")
        self.assertIsNone(self.oracle.lanes[Lane.CONTROL])
        self.assertEqual(self.oracle.out_of_order_drops, 1)

    def test_adv_orphan_fragment_rejection(self):
        """Stress: Feeding fragment with frag_idx > 0 when lane is inactive is rejected."""
        f1 = FragmentMutator.pack_canonical_fragment(Lane.BULK, total_frags=3, frag_idx=1, credits=8, chunk=b"Orphan")
        _assembled, status = self.oracle.feed(f1, now_s=50.0)
        self.assertEqual(status, "ERROR_MISSING_FRAG_0")
        self.assertEqual(self.oracle.out_of_order_drops, 1)
        self.assertIsNone(self.oracle.lanes[Lane.BULK])

    # --------------------------------------------------------------------------
    # 4. Length Bounds & Overflow Attack Resistance
    # --------------------------------------------------------------------------
    def test_adv_implied_pre_bounds_attack_rejection(self):
        """Stress: First fragment whose implied minimum length exceeds MAX_FRAME_SIZE is rejected."""
        # 100 fragments, each claiming 100 bytes -> 99 * 100 = 9900 bytes > 2068 MAX_FRAME_SIZE
        malicious_f0 = FragmentMutator.pack_canonical_fragment(
            lane=Lane.BULK, total_frags=100, frag_idx=0, credits=8, chunk=b"A" * 100
        )
        _assembled, status = self.oracle.feed(malicious_f0, now_s=60.0)
        self.assertEqual(status, "ERROR_PRE_BOUNDS_EXCEEDED")
        self.assertEqual(self.oracle.length_violations, 1)
        self.assertIsNone(self.oracle.lanes[Lane.BULK])

    def test_adv_cumulative_length_overflow_rejection(self):
        """Stress: Cumulative payload exceeding MAX_FRAME_SIZE during stream is rejected."""
        f0 = FragmentMutator.pack_canonical_fragment(Lane.BULK, total_frags=2, frag_idx=0, credits=8, chunk=b"A" * 100)
        # Second fragment causes cumulative total to exceed MAX_FRAME_SIZE (2068 bytes)
        f1 = FragmentMutator.pack_canonical_fragment(Lane.BULK, total_frags=2, frag_idx=1, credits=8, chunk=b"B" * (MAX_FRAME_SIZE + 1))

        self.oracle.feed(f0, now_s=70.0)
        _assembled, status = self.oracle.feed(f1, now_s=70.05)
        self.assertEqual(status, "ERROR_LENGTH_VIOLATION")
        self.assertEqual(self.oracle.length_violations, 1)
        self.assertIsNone(self.oracle.lanes[Lane.BULK])

    # --------------------------------------------------------------------------
    # 5. Reassembly Timeout
    # --------------------------------------------------------------------------
    def test_adv_reassembly_timeout_expiration(self):
        """Stress: Gap exceeding 2.0s triggers TIMEOUT and resets lane state."""
        f0 = FragmentMutator.pack_canonical_fragment(Lane.BULK, total_frags=2, frag_idx=0, credits=8, chunk=b"P0")
        f1 = FragmentMutator.pack_canonical_fragment(Lane.BULK, total_frags=2, frag_idx=1, credits=8, chunk=b"P1")

        self.oracle.feed(f0, now_s=100.0)
        self.assertIsNotNone(self.oracle.lanes[Lane.BULK])

        # Advance by 2.01s (> REASSEMBLY_TIMEOUT_S)
        _assembled, status = self.oracle.feed(f1, now_s=100.0 + REASSEMBLY_TIMEOUT_S + 0.01)
        self.assertEqual(status, "TIMEOUT")
        self.assertEqual(self.oracle.frag_timeouts, 1)
        self.assertIsNone(self.oracle.lanes[Lane.BULK])

    # --------------------------------------------------------------------------
    # 6. Concurrent Bi-Lane Interleaving
    # --------------------------------------------------------------------------
    def test_adv_concurrent_bi_lane_interleaving(self):
        """Stress: Control lane and Bulk lane fragments interleaved with zero cross-talk."""
        raw_ctrl = b"CONTROL_COMMAND_PAYLOAD_TEST_DATA"
        raw_bulk = bytes([i % 256 for i in range(1024)])

        ctrl_frags = FragmentMutator.slice_message(raw_ctrl, lane=Lane.CONTROL, mtu=23)
        bulk_frags = FragmentMutator.slice_message(raw_bulk, lane=Lane.BULK, mtu=23)

        ctrl_assembled = None
        bulk_assembled = None
        t = 200.0

        # Interleave 1 control frag, 2 bulk frags
        max(len(ctrl_frags), len(bulk_frags))
        c_i = 0
        b_i = 0

        while c_i < len(ctrl_frags) or b_i < len(bulk_frags):
            t += 0.01
            if c_i < len(ctrl_frags):
                res, st = self.oracle.feed(ctrl_frags[c_i], now_s=t)
                if st == "COMPLETE":
                    ctrl_assembled = res
                c_i += 1

            for _ in range(2):
                if b_i < len(bulk_frags):
                    t += 0.005
                    res, st = self.oracle.feed(bulk_frags[b_i], now_s=t)
                    if st == "COMPLETE":
                        bulk_assembled = res
                    b_i += 1

        self.assertEqual(ctrl_assembled, raw_ctrl, "Control lane payload must match exactly")
        self.assertEqual(bulk_assembled, raw_bulk, "Bulk lane payload must match exactly")
        self.assertEqual(self.oracle.out_of_order_drops, 0)
        self.assertEqual(self.oracle.frag_timeouts, 0)
        self.assertEqual(self.oracle.length_violations, 0)

    # --------------------------------------------------------------------------
    # 7. Credit Flow Control & Lane Isolation
    # --------------------------------------------------------------------------
    def test_adv_flow_control_lane_0_reservation(self):
        """Stress: Bulk lane consumption down to 0 credits must NEVER starve Control lane."""
        fc = FlowController(initial_credits=8)
        self.assertEqual(fc.credits, 8)

        # Bulk lane consumes all 7 available non-reserved credits
        for i in range(7):
            self.assertTrue(fc.can_send(LaneType.BULK), f"Bulk can send at credit {fc.credits}")
            self.assertTrue(fc.consume(LaneType.BULK))

        # At 1 credit left (the reserved credit for Lane 0):
        self.assertEqual(fc.credits, 1)
        self.assertFalse(fc.can_send(LaneType.BULK), "Bulk must NOT be able to send when credits <= 1")
        self.assertTrue(fc.can_send(LaneType.CONTROL), "Control lane MUST be able to send using reserved credit")

        # Control lane consumes the reserved credit
        self.assertTrue(fc.consume(LaneType.CONTROL))
        self.assertEqual(fc.credits, 0)

        # At 0 credits left: Bulk blocked, Control blocked until granted
        self.assertFalse(fc.can_send(LaneType.BULK))
        self.assertFalse(fc.consume(LaneType.BULK))
        self.assertFalse(fc.can_send(LaneType.CONTROL))

        # Grant credits restores transmission
        fc.grant_credits(4)
        self.assertEqual(fc.credits, 4)
        self.assertTrue(fc.can_send(LaneType.BULK))
        self.assertTrue(fc.can_send(LaneType.CONTROL))


if __name__ == "__main__":
    unittest.main()
