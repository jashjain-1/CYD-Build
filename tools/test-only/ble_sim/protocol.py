"""DashCue Wire Protocol Framing, Fragmentation, Deduplication and Flow Control.

Opaque-box protocol implementation for test harness and BLE simulator.
References:
- docs/PROTOCOL.md
- protocol/dashcue.proto
- protocol/uuids.md
"""

from __future__ import annotations

import struct
import time
import zlib
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Dict, List, Optional, Tuple

try:
    from tools.test_only.ble_sim import dashcue_pb2 as pb
except ImportError:
    try:
        import dashcue_pb2 as pb
    except ImportError:
        from . import dashcue_pb2 as pb

# Protocol Constants
PROTO_MAJOR = 1
PROTO_MINOR = 0
MAGIC = 0xD45C
MAX_PAYLOAD_LEN = 4096
ENVELOPE_HEADER_LEN = 16
TRAILER_LEN = 4  # CRC32
MIN_ENVELOPE_LEN = ENVELOPE_HEADER_LEN + TRAILER_LEN
FRAGMENT_HEADER_LEN = 4
REASSEMBLY_TIMEOUT_S = 2.0
DEDUPE_WINDOW_SIZE = 32
DEDUPE_TTL_S = 60.0
INITIAL_FLOW_CREDITS = 8
CONFIRM_TIMEOUT_DEFAULT_S = 1.5
CONFIRM_TIMEOUT_NEXT_PREV_S = 3.0

# Service & Characteristic UUIDs (frozen per protocol/uuids.md)
UUID_DASHCUE_SERVICE = "7d1a0001-5c3e-4b8a-9f21-3a6c8e0d4b10"
UUID_DASHCUE_RX = "7d1a0002-5c3e-4b8a-9f21-3a6c8e0d4b10"
UUID_DASHCUE_TX = "7d1a0003-5c3e-4b8a-9f21-3a6c8e0d4b10"
UUID_DIS_SERVICE = "0000180a-0000-1000-8000-00805f9b34fb"
UUID_DIS_FW_REV = "00002a26-0000-1000-8000-00805f9b34fb"
UUID_DIS_HW_REV = "00002a27-0000-1000-8000-00805f9b34fb"
UUID_DIS_SERIAL = "00002a25-0000-1000-8000-00805f9b34fb"


class ProtocolError(Exception):
    """Protocol error carrying DashCue ErrorCode."""

    def __init__(self, code: int, message: str, correlation: int = 0):
        super().__init__(f"{message} (code={code}, correlation={correlation})")
        self.code = code
        self.correlation = correlation


@dataclass
class Envelope:
    """DashCue 16-byte Envelope Header (<HBBHIIH) + Payload + IEEE 802.3 CRC32 Trailer."""

    magic: int = MAGIC
    version: int = PROTO_MAJOR
    flags: int = 0
    msg_type: int = 0
    msg_id: int = 0
    payload_len: int = 0
    reserved: int = 0
    payload: bytes = b""
    crc32: int = 0

    @property
    def seq(self) -> int:
        return self.msg_id

    @seq.setter
    def seq(self, val: int) -> None:
        self.msg_id = val

    @property
    def session_id(self) -> int:
        return self.reserved

    @session_id.setter
    def session_id(self, val: int) -> None:
        self.reserved = val

    @property
    def ver_major(self) -> int:
        return self.version

    @property
    def ver_minor(self) -> int:
        return 0

    @property
    def correlation(self) -> int:
        return self.msg_id

    @classmethod
    def create(
        cls,
        msg_type: int,
        payload: bytes,
        session_id: int = 1,
        seq: int = 1,
        correlation: int = 0,
        flags: int = 0,
        ver_major: int = PROTO_MAJOR,
        ver_minor: int = PROTO_MINOR,
        magic: int = MAGIC,
        msg_id: Optional[int] = None,
        reserved: int = 0,
    ) -> "Envelope":
        if len(payload) > MAX_PAYLOAD_LEN:
            raise ProtocolError(
                pb.ErrorCode.EC_TOO_LARGE,
                f"Payload size {len(payload)} exceeds max {MAX_PAYLOAD_LEN}",
                correlation=correlation or seq,
            )
        eff_msg_id = msg_id if msg_id is not None else (correlation if correlation != 0 else seq)
        eff_reserved = reserved if reserved != 0 else session_id
        env = cls(
            magic=magic,
            version=ver_major,
            flags=flags,
            msg_type=msg_type,
            msg_id=eff_msg_id,
            payload_len=len(payload),
            reserved=eff_reserved,
            payload=payload,
        )
        env.crc32 = env.compute_crc32()
        return env

    def compute_crc32(self) -> int:
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
        crc = struct.pack("<I", self.crc32)
        return header + self.payload + crc

    @classmethod
    def unpack(cls, data: bytes) -> "Envelope":
        if len(data) < MIN_ENVELOPE_LEN:
            raise ProtocolError(
                pb.ErrorCode.EC_DECODE,
                f"Data length {len(data)} shorter than minimum envelope {MIN_ENVELOPE_LEN}",
            )

        header_bytes = data[:ENVELOPE_HEADER_LEN]
        (
            magic,
            version,
            flags,
            msg_type,
            msg_id,
            payload_len,
            reserved,
        ) = struct.unpack("<HBBHIIH", header_bytes)

        if payload_len > MAX_PAYLOAD_LEN:
            raise ProtocolError(
                pb.ErrorCode.EC_TOO_LARGE,
                f"Declared payload length {payload_len} exceeds max {MAX_PAYLOAD_LEN}",
                correlation=msg_id,
            )

        expected_total = ENVELOPE_HEADER_LEN + payload_len + TRAILER_LEN
        if len(data) != expected_total:
            raise ProtocolError(
                pb.ErrorCode.EC_DECODE,
                f"Data length {len(data)} does not match expected total {expected_total}",
                correlation=msg_id,
            )

        payload = data[ENVELOPE_HEADER_LEN : ENVELOPE_HEADER_LEN + payload_len]
        (received_crc,) = struct.unpack("<I", data[ENVELOPE_HEADER_LEN + payload_len :])

        computed_crc = zlib.crc32(header_bytes + payload) & 0xFFFFFFFF
        if received_crc != computed_crc:
            raise ProtocolError(
                pb.ErrorCode.EC_CRC,
                f"CRC mismatch: received 0x{received_crc:08X}, computed 0x{computed_crc:08X}",
                correlation=msg_id,
            )

        if magic != MAGIC:
            raise ProtocolError(
                pb.ErrorCode.EC_UNKNOWN_TYPE,
                f"Invalid magic: received 0x{magic:04X}, expected 0x{MAGIC:04X}",
                correlation=msg_id,
            )

        if version != PROTO_MAJOR:
            raise ProtocolError(
                pb.ErrorCode.EC_VERSION_MISMATCH,
                f"Major version mismatch: received {version}, expected {PROTO_MAJOR}",
                correlation=msg_id,
            )

        return cls(
            magic=magic,
            version=version,
            flags=flags,
            msg_type=msg_type,
            msg_id=msg_id,
            payload_len=payload_len,
            reserved=reserved,
            payload=payload,
            crc32=received_crc,
        )


class Fragmenter:
    """Splits an envelope byte stream into ATT chunks with a 4-byte canonical header (<BBBB)."""

    @staticmethod
    def fragment(
        msg_id: int = 0,
        message_bytes: bytes = b"",
        mtu: int = 247,
        lane: int = 0,
        credits: int = 8,
    ) -> List[bytes]:
        effective_att = max(20, mtu - 3)
        chunk_size = effective_att - FRAGMENT_HEADER_LEN
        if chunk_size <= 0:
            raise ValueError(f"MTU {mtu} is too small for fragmentation header")

        total_bytes = len(message_bytes)
        if total_bytes == 0:
            frag_cnt = 1
            header = struct.pack("<BBBB", lane, frag_cnt, 0, credits)
            return [header]

        frag_cnt = (total_bytes + chunk_size - 1) // chunk_size
        if frag_cnt > 255:
            raise ProtocolError(
                pb.ErrorCode.EC_TOO_LARGE,
                f"Fragment count {frag_cnt} exceeds maximum 255",
            )

        fragments: List[bytes] = []
        for i in range(frag_cnt):
            start = i * chunk_size
            end = min(total_bytes, start + chunk_size)
            chunk = message_bytes[start:end]
            header = struct.pack("<BBBB", lane, frag_cnt, i, credits)
            fragments.append(header + chunk)

        return fragments


class LaneType(IntEnum):
    CONTROL = 0  # High priority commands and state
    BULK = 1  # Route overview, artwork, diagnostics


@dataclass
class ReassemblyContext:
    msg_id: int
    frag_cnt: int
    expected_idx: int
    received_chunks: List[bytes]
    total_bytes: int
    start_time: float


class Reassembler:
    """2-Lane Fragment Reassembler enforcing bounds, ordering, and 2.0s timeouts."""

    def __init__(self):
        self.lanes: Dict[int, Optional[ReassemblyContext]] = {
            LaneType.CONTROL: None,
            LaneType.BULK: None,
        }
        self.frag_timeouts = 0
        self.frag_drops = 0

    def get_lane_for_type(self, msg_type: int) -> int:
        if msg_type in (
            pb.MessageType.MT_ARTWORK_BEGIN,
            pb.MessageType.MT_ARTWORK_CHUNK,
            pb.MessageType.MT_ARTWORK_END,
            pb.MessageType.MT_ROUTE_OVERVIEW,
            pb.MessageType.MT_DIAG_RESP,
        ):
            return LaneType.BULK
        return LaneType.CONTROL

    def reset_lane(self, lane: int) -> None:
        self.lanes[lane] = None

    def check_timeouts(self, now: Optional[float] = None) -> None:
        current_time = now if now is not None else time.time()
        for lane, ctx in list(self.lanes.items()):
            if ctx is not None:
                if current_time - ctx.start_time > REASSEMBLY_TIMEOUT_S:
                    self.frag_timeouts += 1
                    self.lanes[lane] = None

    def push_fragment(
        self,
        fragment: bytes,
        lane: int = LaneType.CONTROL,
        now: Optional[float] = None,
    ) -> Optional[bytes]:
        current_time = now if now is not None else time.time()
        self.check_timeouts(current_time)

        if len(fragment) < FRAGMENT_HEADER_LEN:
            self.frag_drops += 1
            return None

        # Support both canonical <BBBB (lane, total_frags, frag_idx, credits) and legacy <HBB (msg_id, frag_idx, frag_cnt)
        b0, b1, b2, b3 = struct.unpack("<BBBB", fragment[:FRAGMENT_HEADER_LEN])
        if b0 not in (0, 1) or (b1 == 0 and b3 > 0):
            msg_id, frag_idx, frag_cnt = struct.unpack("<HBB", fragment[:FRAGMENT_HEADER_LEN])
            frag_lane = lane
        else:
            frag_lane = b0
            frag_cnt = b1
            frag_idx = b2
            msg_id = 0

        target_lane = lane if lane != LaneType.CONTROL else frag_lane
        chunk = fragment[FRAGMENT_HEADER_LEN:]

        if frag_cnt == 0 or frag_idx >= frag_cnt:
            self.frag_drops += 1
            return None

        # Pre-allocation bounds check
        if frag_cnt * len(chunk) > MAX_PAYLOAD_LEN + ENVELOPE_HEADER_LEN + TRAILER_LEN + 1024:
            self.frag_drops += 1
            self.lanes[target_lane] = None
            raise ProtocolError(
                pb.ErrorCode.EC_TOO_LARGE,
                f"Reassembly pre-bounds exceeded: {frag_cnt} * {len(chunk)} > 4096",
            )

        ctx = self.lanes[target_lane]
        if frag_idx == 0:
            # Start new reassembly
            ctx = ReassemblyContext(
                msg_id=msg_id,
                frag_cnt=frag_cnt,
                expected_idx=1,
                received_chunks=[chunk],
                total_bytes=len(chunk),
                start_time=current_time,
            )
            self.lanes[target_lane] = ctx
            if frag_cnt == 1:
                self.lanes[target_lane] = None
                return chunk
            return None

        # Existing reassembly checks
        if ctx is None or frag_cnt != ctx.frag_cnt or (msg_id != 0 and ctx.msg_id != 0 and ctx.msg_id != msg_id):
            # Out of order or unmatched fragment
            self.frag_drops += 1
            self.lanes[target_lane] = None
            return None

        if frag_idx != ctx.expected_idx:
            # Duplicate or out of order
            self.frag_drops += 1
            self.lanes[target_lane] = None
            return None

        ctx.received_chunks.append(chunk)
        ctx.total_bytes += len(chunk)
        ctx.expected_idx += 1

        if ctx.total_bytes > MAX_PAYLOAD_LEN + ENVELOPE_HEADER_LEN + TRAILER_LEN:
            self.frag_drops += 1
            self.lanes[target_lane] = None
            raise ProtocolError(
                pb.ErrorCode.EC_TOO_LARGE,
                f"Reassembled size {ctx.total_bytes} exceeds limit",
            )

        if ctx.expected_idx == ctx.frag_cnt:
            assembled = b"".join(ctx.received_chunks)
            self.lanes[target_lane] = None
            return assembled

        return None


@dataclass
class DedupeEntry:
    command_id: int
    cmd_result: pb.CmdResult
    timestamp: float


class DeduplicationCache:
    """Sliding window of 32 command IDs with 60 s TTL."""

    def __init__(self, capacity: int = DEDUPE_WINDOW_SIZE, ttl: float = DEDUPE_TTL_S):
        self.capacity = capacity
        self.ttl = ttl
        self.entries: List[DedupeEntry] = []

    def lookup(
        self, command_id: int, now: Optional[float] = None
    ) -> Optional[pb.CmdResult]:
        current_time = now if now is not None else time.time()
        self._prune(current_time)
        for entry in self.entries:
            if entry.command_id == command_id:
                return entry.cmd_result
        return None

    def store(
        self,
        command_id: int,
        cmd_result: pb.CmdResult,
        now: Optional[float] = None,
    ) -> None:
        current_time = now if now is not None else time.time()
        self._prune(current_time)
        # Check if already exists; if so, update
        for entry in self.entries:
            if entry.command_id == command_id:
                entry.cmd_result = cmd_result
                entry.timestamp = current_time
                return

        if len(self.entries) >= self.capacity:
            self.entries.pop(0)  # FIFO ring eviction

        self.entries.append(
            DedupeEntry(
                command_id=command_id,
                cmd_result=cmd_result,
                timestamp=current_time,
            )
        )

    def _prune(self, current_time: float) -> None:
        self.entries = [
            e for e in self.entries if (current_time - e.timestamp) <= self.ttl
        ]


class FlowController:
    """Credit-based flow controller with Lane 0 reservation."""

    def __init__(self, initial_credits: int = INITIAL_FLOW_CREDITS):
        self.credits = initial_credits
        self.reserved_lane0 = 1

    def grant_credits(self, amount: int) -> None:
        self.credits += amount

    def can_send(self, lane: int) -> bool:
        if lane == LaneType.CONTROL:
            return self.credits > 0
        # Bulk lane cannot consume the last reserved credit
        return self.credits > self.reserved_lane0

    def consume(self, lane: int) -> bool:
        if not self.can_send(lane):
            return False
        self.credits -= 1
        return True


def create_error_response(
    correlation: int,
    code: int,
    text: str = "",
    detail: int = 0,
    session_id: int = 1,
    seq: int = 1,
) -> Envelope:
    err = pb.ErrorMsg(
        correlation=correlation,
        code=code,
        detail=detail,
        text=text[:48],
    )
    return Envelope.create(
        msg_type=pb.MessageType.MT_ERROR,
        payload=err.SerializeToString(),
        session_id=session_id,
        seq=seq,
        correlation=correlation,
        flags=pb.EnvelopeFlag.EF_IS_RESPONSE,
    )


def create_cmd_result(
    command_id: int,
    status: int,
    reason: int = 0,
    observed_at_ms: int = 0,
    session_id: int = 1,
    seq: int = 1,
) -> Envelope:
    res = pb.CmdResult(
        command_id=command_id,
        status=status,
        reason=reason,
        observed_at_ms=observed_at_ms,
    )
    return Envelope.create(
        msg_type=pb.MessageType.MT_CMD_RESULT,
        payload=res.SerializeToString(),
        session_id=session_id,
        seq=seq,
        correlation=command_id,
        flags=pb.EnvelopeFlag.EF_IS_RESPONSE,
    )
