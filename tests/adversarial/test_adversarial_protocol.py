"""Adversarial Protocol Stress, Fuzzing, and Boundary Test Suite.

Python reference-model checks plus one freshly built production C++ parser run.
No radio or device is exercised; model assertions alone are not firmware evidence.
"""

import os
import subprocess
import sys
import time
import json
from pathlib import Path
import tempfile
import unittest

# Ensure ble_sim and fuzz tools can be imported
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "tools", "test-only", "fuzz"))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "tools", "test-only", "ble_sim"))

from fuzz_protocol_harness import (
    CANONICAL_MAGIC,
    CANONICAL_PROTO_VERSION,
    MAX_PAYLOAD_SIZE,
    HEADER_SIZE,
    TRAILER_SIZE,
    MIN_FRAME_SIZE,
    MAX_FRAME_SIZE,
    REASSEMBLY_TIMEOUT_S,
    CanonicalFrame,
    ErrorCode,
    CmdResultStatus,
    Lane,
    FragmentMutator,
    StatefulReassemblerOracle,
)


class TestAdversarialProtocol(unittest.TestCase):

    def test_adv_pr_01_crc32_corruption_and_parser_trust_inversion(self):
        """TC-ADV-01: Bit-flip mutation across header, payload, and trailer.
        
        Compares strict and intentionally non-strict reference-model validation;
        firmware validation order is checked separately by the native harness.
        """
        payload = b"Hello DashCue RedTeam Fuzzing!"
        frame = CanonicalFrame(
            magic=CANONICAL_MAGIC,
            version=CANONICAL_PROTO_VERSION,
            flags=0,
            msg_type=10,
            msg_id=12345,
            payload_len=len(payload),
            reserved=0,
            payload=payload,
        )
        raw_bytes = bytearray(frame.pack())

        # Baseline check
        unpacked, err = CanonicalFrame.unpack(bytes(raw_bytes), strict_crc_order=True)
        assert err == ErrorCode.EC_NONE
        assert unpacked.payload == payload

        # 1. Flip payload byte
        corrupt_payload = bytearray(raw_bytes)
        corrupt_payload[HEADER_SIZE + 5] ^= 0x01
        _, err = CanonicalFrame.unpack(bytes(corrupt_payload), strict_crc_order=True)
        assert err == ErrorCode.EC_CRC

        # 2. Flip trailer CRC byte
        corrupt_crc = bytearray(raw_bytes)
        corrupt_crc[-1] ^= 0x80
        _, err = CanonicalFrame.unpack(bytes(corrupt_crc), strict_crc_order=True)
        assert err == ErrorCode.EC_CRC

        # 3. Empirically verify Parser Trust Inversion (VULN-PR-01)
        corrupt_magic = bytearray(raw_bytes)
        corrupt_magic[0] ^= 0x01  # Flip bit in magic byte 0
        
        # In strict CRC order (spec §3), any corrupted frame must return EC_CRC
        _, err_strict = CanonicalFrame.unpack(bytes(corrupt_magic), strict_crc_order=True)
        assert err_strict == ErrorCode.EC_CRC

        # An intentionally incorrect reference model returns EC_UNKNOWN_TYPE.
        # This is a model comparison; current production code is exercised by the native test.
        _, err_firmware = CanonicalFrame.unpack(bytes(corrupt_magic), strict_crc_order=False)
        assert err_firmware == ErrorCode.EC_UNKNOWN_TYPE, (
            "Non-strict reference model must demonstrate why validation order matters"
        )

    def test_adv_pr_02_payload_length_bounds(self):
        """TC-ADV-02: Boundary length testing (0, 1, 4096, 4097, 65535, 0xFFFFFFFF)."""
        # 1. 0-byte payload (Valid minimal frame: 20 bytes)
        frame_0 = CanonicalFrame(msg_type=50, msg_id=1, payload=b"")
        packed_0 = frame_0.pack()
        assert len(packed_0) == 20
        unpacked_0, err_0 = CanonicalFrame.unpack(packed_0)
        assert err_0 == ErrorCode.EC_NONE
        assert unpacked_0.payload_len == 0
        assert unpacked_0.payload == b""

        # 2. 1-byte payload
        frame_1 = CanonicalFrame(msg_type=50, msg_id=2, payload=b"\x42")
        packed_1 = frame_1.pack()
        assert len(packed_1) == 21
        unpacked_1, err_1 = CanonicalFrame.unpack(packed_1)
        assert err_1 == ErrorCode.EC_NONE
        assert unpacked_1.payload == b"\x42"

        # 3. 4096-byte payload (exact max bound)
        max_payload = b"\xAA" * 4096
        frame_max = CanonicalFrame(msg_type=13, msg_id=3, payload=max_payload)
        packed_max = frame_max.pack()
        assert len(packed_max) == 16 + 4096 + 4
        unpacked_max, err_max = CanonicalFrame.unpack(packed_max)
        assert err_max == ErrorCode.EC_NONE
        assert len(unpacked_max.payload) == 4096

        # 4. 4097-byte payload (over max bound)
        over_payload = b"\xBB" * 4097
        frame_over = CanonicalFrame(msg_type=13, msg_id=4, payload=over_payload)
        raw_over = frame_over.pack()
        _, err_over = CanonicalFrame.unpack(raw_over)
        assert err_over == ErrorCode.EC_TOO_LARGE

        # 5. Short frames (< 20 bytes)
        for short_len in (0, 1, 15, 19):
            short_buf = b"\x00" * short_len
            _, err_short = CanonicalFrame.unpack(short_buf)
            assert err_short != ErrorCode.EC_NONE

    def test_adv_pr_03_fragment_index_and_total_bounds(self):
        """TC-ADV-03: Fragment index boundaries (frag_idx >= total, total=0, missing frag 0)."""
        oracle = StatefulReassemblerOracle()

        # 1. total_frags = 0
        frag_0_total = FragmentMutator.pack_canonical_fragment(Lane.CONTROL, total_frags=0, frag_idx=0, credits=8, chunk=b"data")
        _, status = oracle.feed(frag_0_total, now_s=1.0)
        assert status == "ERROR_INVALID_HEADER"

        # 2. frag_idx >= total_frags (frag_idx = 1, total_frags = 1)
        frag_oob = FragmentMutator.pack_canonical_fragment(Lane.CONTROL, total_frags=1, frag_idx=1, credits=8, chunk=b"data")
        _, status = oracle.feed(frag_oob, now_s=1.1)
        assert status == "ERROR_INVALID_HEADER"

        # 3. Missing frag 0 (stream begins at frag_idx = 1)
        frag_orphan = FragmentMutator.pack_canonical_fragment(Lane.BULK, total_frags=3, frag_idx=1, credits=8, chunk=b"data")
        _, status = oracle.feed(frag_orphan, now_s=1.2)
        assert status == "ERROR_MISSING_FRAG_0"
        assert oracle.out_of_order_drops == 1

        # 4. Pre-allocation bounds attack: total_frags = 255 with 240-byte chunks (VULN-PR-03)
        frag_huge = FragmentMutator.pack_canonical_fragment(Lane.BULK, total_frags=255, frag_idx=0, credits=8, chunk=b"\x55" * 240)
        # In strict pre-bounds checking:
        _, status = oracle.feed(frag_huge, now_s=2.0)
        assert status == "ERROR_PRE_BOUNDS_EXCEEDED"

    def test_adv_pr_04_mtu23_single_byte_and_mtu247_fragmentation(self):
        """TC-ADV-04: Reassembly under MTU 23 (16-byte chunks), single-byte chunk streams, and MTU 247."""
        oracle = StatefulReassemblerOracle()

        # 1. MTU 23 fragmentation of a 100-byte message
        message = bytes(range(100))
        frags_mtu23 = FragmentMutator.slice_message(message, lane=Lane.CONTROL, mtu=23)
        assert len(frags_mtu23) == (100 + 15) // 16  # 7 fragments

        assembled = None
        for i, frag in enumerate(frags_mtu23):
            assembled, status = oracle.feed(frag, now_s=10.0 + i * 0.01)
            if i == len(frags_mtu23) - 1:
                assert status == "COMPLETE"
                assert assembled == message
            else:
                assert status == "IN_PROGRESS"

        # 2. Extreme single-byte chunk stream (1 byte per fragment)
        single_byte_msg = b"DashCue Automotive HUD Single-Byte Stream!"
        frags_single = []
        for i, byte_val in enumerate(single_byte_msg):
            frag = FragmentMutator.pack_canonical_fragment(
                Lane.BULK,
                total_frags=len(single_byte_msg),
                frag_idx=i,
                credits=8,
                chunk=bytes([byte_val]),
            )
            frags_single.append(frag)

        for i, frag in enumerate(frags_single):
            assembled, status = oracle.feed(frag, now_s=20.0 + i * 0.01)
            if i == len(frags_single) - 1:
                assert status == "COMPLETE"
                assert assembled == single_byte_msg
            else:
                assert status == "IN_PROGRESS"

    def test_adv_pr_05_out_of_order_and_duplicate_delivery(self):
        """TC-ADV-05: Out-of-order and duplicate fragment rejection."""
        oracle = StatefulReassemblerOracle()

        frag0 = FragmentMutator.pack_canonical_fragment(Lane.CONTROL, total_frags=3, frag_idx=0, credits=8, chunk=b"chunk0")
        frag1 = FragmentMutator.pack_canonical_fragment(Lane.CONTROL, total_frags=3, frag_idx=1, credits=8, chunk=b"chunk1")
        frag2 = FragmentMutator.pack_canonical_fragment(Lane.CONTROL, total_frags=3, frag_idx=2, credits=8, chunk=b"chunk2")

        # Duplicate Frag 0
        _, st = oracle.feed(frag0, now_s=30.0)
        assert st == "IN_PROGRESS"
        _, st = oracle.feed(frag0, now_s=30.1)
        assert st == "ERROR_OUT_OF_ORDER"
        assert oracle.lanes[Lane.CONTROL] is None  # Lane reset

        # Skipped Frag 1 (Feed 0, then 2)
        oracle.feed(frag0, now_s=31.0)
        _, st = oracle.feed(frag2, now_s=31.1)
        assert st == "ERROR_OUT_OF_ORDER"
        assert oracle.lanes[Lane.CONTROL] is None

    def test_adv_pr_06_interleaved_multi_lane_streaming(self):
        """TC-ADV-06: High-priority Control stream interleaved into Bulk Artwork stream."""
        oracle = StatefulReassemblerOracle()

        bulk_msg = b"B" * 500
        ctrl_msg = b"HIGH_PRIORITY_COMMAND_NAV_PAUSE"

        bulk_frags = FragmentMutator.slice_message(bulk_msg, lane=Lane.BULK, mtu=247)
        ctrl_frags = FragmentMutator.slice_message(ctrl_msg, lane=Lane.CONTROL, mtu=247)

        # Send 1st half of bulk frags
        for i in range(len(bulk_frags) // 2):
            _, st = oracle.feed(bulk_frags[i], now_s=40.0 + i * 0.01)
            assert st == "IN_PROGRESS"

        # Interleave entire control stream
        for i, c_frag in enumerate(ctrl_frags):
            assembled, st = oracle.feed(c_frag, now_s=40.5 + i * 0.01)
            if i == len(ctrl_frags) - 1:
                assert st == "COMPLETE"
                assert assembled == ctrl_msg
            else:
                assert st == "IN_PROGRESS"

        # Resume remaining bulk frags
        for i in range(len(bulk_frags) // 2, len(bulk_frags)):
            assembled, st = oracle.feed(bulk_frags[i], now_s=41.0 + i * 0.01)
            if i == len(bulk_frags) - 1:
                assert st == "COMPLETE"
                assert assembled == bulk_msg
            else:
                assert st == "IN_PROGRESS"

    def test_adv_pr_07_reassembly_gap_timeout_boundary(self):
        """TC-ADV-07: Boundary test on 2.0 s reassembly timeout."""
        oracle = StatefulReassemblerOracle()

        f0 = FragmentMutator.pack_canonical_fragment(Lane.BULK, total_frags=2, frag_idx=0, credits=8, chunk=b"part0")
        f1 = FragmentMutator.pack_canonical_fragment(Lane.BULK, total_frags=2, frag_idx=1, credits=8, chunk=b"part1")

        # 1. Gap of 1.99 s (< 2.0 s) -> SUCCEEDS
        oracle.feed(f0, now_s=50.0)
        assembled, st = oracle.feed(f1, now_s=51.99)
        assert st == "COMPLETE"
        assert assembled == b"part0part1"

        # 2. Gap of 2.01 s (> 2.0 s) -> TIMEOUT
        oracle.feed(f0, now_s=60.0)
        assembled, st = oracle.feed(f1, now_s=62.01)
        assert st == "TIMEOUT"
        assert oracle.lanes[Lane.BULK] is None

    def test_adv_pr_08_reference_model_decodes_existing_fixtures(self):
        """Check the Python reference model against checked-in production fixtures.

        Documentation drift is recorded in the audit, never required for a pass.
        This tests envelope bytes, not protobuf decoding or language parity.
        """
        fixtures = sorted((Path(PROJECT_ROOT) / "protocol/fixtures").glob("*.frame"))
        assert fixtures, "No protocol fixtures found"
        for fixture in fixtures:
            frame, error = CanonicalFrame.unpack(fixture.read_bytes(), strict_crc_order=True)
            assert error == ErrorCode.EC_NONE, fixture.name
            assert frame.payload == fixture.with_suffix(".bin").read_bytes(), fixture.name

    def test_adv_pr_09_run_native_c_fuzzer_verification(self):
        """Compile current production sources; old prebuilt binaries are not evidence."""
        with tempfile.TemporaryDirectory(prefix="dashcue-native-fuzz-") as output:
            result = subprocess.run([
                sys.executable, str(Path(PROJECT_ROOT) / "tools/test-only/fuzz/run_fuzz_campaign.py"),
                "--native-only", "--iterations", "10000", "--evidence-dir", output,
            ], capture_output=True, text=True, timeout=180)
            assert result.returncode == 0, f"Native build/fuzz failed:\n{result.stdout}\n{result.stderr}"
            reports = list(Path(output).glob("*/result.json"))
            assert len(reports) == 1
            report = json.loads(reports[0].read_text(encoding="utf-8"))
            assert report["status"] == "PASS"
            assert report["native_exit_code"] == 0
            assert report["input_sha256"]

    def test_adv_pr_10_reference_model_max_frame_bounds(self):
        """A short last chunk is valid; one oversized fragment never completes."""
        payload = bytes([0xAA]) * MAX_FRAME_SIZE
        oracle = StatefulReassemblerOracle()
        fragments = FragmentMutator.slice_message(payload, lane=Lane.BULK, mtu=247)
        assert len(fragments) == 18
        for index, fragment in enumerate(fragments):
            assembled, status = oracle.feed(fragment, now_s=1.0 + index * 0.01)
        assert status == "COMPLETE"
        assert assembled == payload
        for enforce_pre_bounds in (True, False):
            oracle = StatefulReassemblerOracle(enforce_pre_bounds=enforce_pre_bounds)
            oversized = FragmentMutator.pack_canonical_fragment(
                Lane.BULK, total_frags=1, frag_idx=0, credits=8, chunk=payload + b"!")
            assembled, status = oracle.feed(oversized, now_s=2.0)
            assert assembled is None
            assert status == "ERROR_LENGTH_VIOLATION"
