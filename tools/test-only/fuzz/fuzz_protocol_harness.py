"""DashCue Protocol Adversarial Fuzzing and Stress Harness.

Provides generators, oracles, mutators, and stateful test harnesses targeting:
- Envelope framing, CRC32 integrity, and length boundaries
- Fragment indices, total counts, MTU chunk boundaries, and variable chunks
- Out-of-order, duplicate, and dropped fragment delivery
- Interleaved 2-lane streaming (Control vs Bulk)
- Reassembly gap timeouts (2.0s boundary)
- Flow control credit starvation & Lane 0 reservation
- Deduplication cache 32-entry sliding window, replay, and 60.0s TTL expiry
- Protocol specification divergence analysis (Docs vs Project vs Firmware vs BleSim)
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from enum import IntEnum

# Protocol Constants per PROJECT.md and protocol/fixtures/
CANONICAL_MAGIC = 0xD45C
CANONICAL_PROTO_VERSION = 0x01
MAX_PAYLOAD_SIZE = 4096
HEADER_SIZE = 16
TRAILER_SIZE = 4
MIN_FRAME_SIZE = HEADER_SIZE + TRAILER_SIZE  # 20 bytes
MAX_FRAME_SIZE = HEADER_SIZE + MAX_PAYLOAD_SIZE + TRAILER_SIZE  # 4116 bytes
FRAGMENT_HEADER_SIZE = 4
REASSEMBLY_TIMEOUT_S = 2.0
DEDUP_WINDOW_SIZE = 32
DEDUP_TTL_S = 60.0
INITIAL_CREDITS = 8


class ErrorCode(IntEnum):
    EC_NONE = 0
    EC_TOO_LARGE = 1
    EC_CRC = 2
    EC_VERSION_MISMATCH = 3
    EC_SESSION_MISMATCH = 4
    EC_UNKNOWN_TYPE = 5
    EC_DECODE = 6
    EC_UNSUPPORTED = 7
    EC_BUSY = 8
    EC_NOT_BONDED = 9
    EC_INVALID_ARG = 10
    EC_STALE = 11
    EC_NO_SESSION = 12
    EC_PERMISSION = 13
    EC_INTERNAL = 14
    EC_OFFLINE = 15
    EC_NO_ROUTE = 16


class CmdResultStatus(IntEnum):
    ST_ACCEPTED = 0
    ST_CONFIRMED = 1
    ST_FAILED = 2
    ST_UNCONFIRMED = 3


class Lane(IntEnum):
    CONTROL = 0
    BULK = 1


@dataclass
class CanonicalFrame:
    magic: int = CANONICAL_MAGIC
    version: int = CANONICAL_PROTO_VERSION
    flags: int = 0
    msg_type: int = 0
    msg_id: int = 0
    payload_len: int = 0
    reserved: int = 0
    payload: bytes = b""
    crc32: int = 0

    def compute_crc(self) -> int:
        header = struct.pack(
            "<HBBHIIH",
            self.magic,
            self.version,
            self.flags,
            self.msg_type,
            self.msg_id,
            self.payload_len,
            self.reserved,
        )
        return zlib.crc32(header + self.payload) & 0xFFFFFFFF

    def pack(self) -> bytes:
        self.payload_len = len(self.payload)
        self.crc32 = self.compute_crc()
        header = struct.pack(
            "<HBBHIIH",
            self.magic,
            self.version,
            self.flags,
            self.msg_type,
            self.msg_id,
            self.payload_len,
            self.reserved,
        )
        trailer = struct.pack("<I", self.crc32)
        return header + self.payload + trailer

    @classmethod
    def unpack(cls, data: bytes, strict_crc_order: bool = True) -> tuple[CanonicalFrame | None, ErrorCode]:
        """Unpack a framed message following formal specification order.
        
        If strict_crc_order is True:
            Length bounds -> CRC32 -> Magic -> Version
        If False (matching current firmware implementation):
            Length bounds -> Magic -> Version -> CRC32
        """
        if len(data) < MIN_FRAME_SIZE:
            # Short frame: semantically EC_DECODE
            return None, ErrorCode.EC_DECODE

        if len(data) > MAX_FRAME_SIZE:
            return None, ErrorCode.EC_TOO_LARGE

        (
            magic,
            version,
            flags,
            msg_type,
            msg_id,
            payload_len,
            reserved,
        ) = struct.unpack("<HBBHIIH", data[:HEADER_SIZE])

        if payload_len > MAX_PAYLOAD_SIZE:
            return None, ErrorCode.EC_TOO_LARGE

        expected_total = HEADER_SIZE + payload_len + TRAILER_SIZE
        if len(data) != expected_total:
            return None, ErrorCode.EC_DECODE if len(data) < expected_total else ErrorCode.EC_TOO_LARGE

        payload = data[HEADER_SIZE : HEADER_SIZE + payload_len]
        (received_crc,) = struct.unpack("<I", data[HEADER_SIZE + payload_len :])

        if strict_crc_order:
            # Specification Rule: Validate CRC before trusting any header contents
            computed_crc = zlib.crc32(data[: HEADER_SIZE + payload_len]) & 0xFFFFFFFF
            if received_crc != computed_crc:
                return None, ErrorCode.EC_CRC

            if magic != CANONICAL_MAGIC:
                return None, ErrorCode.EC_UNKNOWN_TYPE

            if version != CANONICAL_PROTO_VERSION:
                return None, ErrorCode.EC_VERSION_MISMATCH
        else:
            # Firmware implementation order (Parser Trust Inversion)
            if magic != CANONICAL_MAGIC:
                return None, ErrorCode.EC_UNKNOWN_TYPE

            if version != CANONICAL_PROTO_VERSION:
                return None, ErrorCode.EC_VERSION_MISMATCH

            computed_crc = zlib.crc32(data[: HEADER_SIZE + payload_len]) & 0xFFFFFFFF
            if received_crc != computed_crc:
                return None, ErrorCode.EC_CRC

        return (
            cls(
                magic=magic,
                version=version,
                flags=flags,
                msg_type=msg_type,
                msg_id=msg_id,
                payload_len=payload_len,
                reserved=reserved,
                payload=payload,
                crc32=received_crc,
            ),
            ErrorCode.EC_NONE,
        )


class FragmentMutator:
    """Generates malformed, corrupted, and out-of-order fragment sequences."""

    @staticmethod
    def pack_canonical_fragment(lane: int, total_frags: int, frag_idx: int, credits: int, chunk: bytes) -> bytes:
        header = struct.pack("<BBBB", lane & 0xFF, total_frags & 0xFF, frag_idx & 0xFF, credits & 0xFF)
        return header + chunk

    @staticmethod
    def slice_message(msg_bytes: bytes, lane: int, mtu: int = 247, credits: int = 8) -> list[bytes]:
        chunk_size = max(1, mtu - 7)
        total_len = len(msg_bytes)
        if total_len == 0:
            return [FragmentMutator.pack_canonical_fragment(lane, 1, 0, credits, b"")]

        total_frags = (total_len + chunk_size - 1) // chunk_size
        frags = []
        for i in range(total_frags):
            start = i * chunk_size
            end = min(total_len, start + chunk_size)
            chunk = msg_bytes[start:end]
            frags.append(FragmentMutator.pack_canonical_fragment(lane, total_frags, i, credits, chunk))
        return frags


class StatefulReassemblerOracle:
    """Stateful Oracle model implementing the rigorous fragmentation spec."""

    def __init__(self, enforce_pre_bounds: bool = True):
        self.enforce_pre_bounds = enforce_pre_bounds
        self.lanes: dict[int, dict | None] = {Lane.CONTROL: None, Lane.BULK: None}
        self.frag_timeouts = 0
        self.out_of_order_drops = 0
        self.length_violations = 0

    def feed(self, fragment: bytes, now_s: float) -> tuple[bytes | None, str]:
        if len(fragment) < FRAGMENT_HEADER_SIZE:
            return None, "ERROR_SHORT_FRAGMENT"

        lane, total_frags, frag_idx, _ = struct.unpack("<BBBB", fragment[:FRAGMENT_HEADER_SIZE])
        chunk = fragment[FRAGMENT_HEADER_SIZE:]

        if lane not in (Lane.CONTROL, Lane.BULK) or total_frags == 0 or frag_idx >= total_frags:
            return None, "ERROR_INVALID_HEADER"

        # Check existing lane timeout
        ctx = self.lanes[lane]
        if ctx is not None and (now_s - ctx["last_time"] >= REASSEMBLY_TIMEOUT_S):
            self.frag_timeouts += 1
            self.lanes[lane] = None
            ctx = None
            if frag_idx != 0:
                return None, "TIMEOUT"

        # No single fragment may exceed the total frame bound, even when the
        # optional first-fragment plausibility check is disabled.
        if len(chunk) > MAX_FRAME_SIZE:
            self.length_violations += 1
            self.lanes[lane] = None
            return None, "ERROR_LENGTH_VIOLATION"

        # The final fragment may be shorter than the first. Check a new stream's
        # minimum possible size, not total_frags * first_chunk or an arbitrary slack.
        if (ctx is None and frag_idx == 0 and self.enforce_pre_bounds and
                total_frags > 1 and (total_frags - 1) * len(chunk) >= MAX_FRAME_SIZE):
            self.length_violations += 1
            self.lanes[lane] = None
            return None, "ERROR_PRE_BOUNDS_EXCEEDED"

        # R2 Clean frame restart: sender cleanly aborted previous frame and restarted with frag 0 (BULK lane only)
        if ctx is not None and lane == Lane.BULK and frag_idx == 0:
            ctx = None
            self.lanes[lane] = None

        if ctx is None:
            if frag_idx != 0:
                self.out_of_order_drops += 1
                return None, "ERROR_MISSING_FRAG_0"

            ctx = {
                "total_frags": total_frags,
                "next_idx": 1,
                "chunks": [chunk],
                "total_bytes": len(chunk),
                "last_time": now_s,
            }
            self.lanes[lane] = ctx
            if total_frags == 1:
                self.lanes[lane] = None
                return chunk, "COMPLETE"
            return None, "IN_PROGRESS"

        # Active lane
        if total_frags != ctx["total_frags"] or frag_idx != ctx["next_idx"]:
            self.out_of_order_drops += 1
            self.lanes[lane] = None
            return None, "ERROR_OUT_OF_ORDER"

        if ctx["total_bytes"] + len(chunk) > MAX_FRAME_SIZE:
            self.length_violations += 1
            self.lanes[lane] = None
            return None, "ERROR_LENGTH_VIOLATION"

        ctx["chunks"].append(chunk)
        ctx["total_bytes"] += len(chunk)
        ctx["next_idx"] += 1
        ctx["last_time"] = now_s

        if ctx["next_idx"] >= ctx["total_frags"]:
            assembled = b"".join(ctx["chunks"])
            self.lanes[lane] = None
            return assembled, "COMPLETE"

        return None, "IN_PROGRESS"
