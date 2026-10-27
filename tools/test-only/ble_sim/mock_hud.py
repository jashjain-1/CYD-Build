"""High-Fidelity Virtual HUD Peripheral for Opaque-Box E2E Testing.

Implements all DashCue firmware subsystems in pure Python for deterministic host verification:
- FreeRTOS dual-core task IPC queues (16 slots each)
- 5-point touch calibration affine math and residual error gating
- 7 landscape screens (Media, Nav, Schematic Route Map, Settings, Diag, Calibration, SafeMode)
- NimBLE GATT peripheral simulation with LE Secure Connections & 6-digit passkey
- Driving lock enforcement (speed >= 8 km/h)
- Watchdog SafeMode trigger (>= 3 resets in 60s)
- Protocol reassembly, CRC32, deduplication cache, and credit flow control
"""

from __future__ import annotations

import collections
import struct
import time
import zlib
from dataclasses import dataclass
from enum import Enum

try:
    from tools.test_only.ble_sim import dashcue_pb2 as pb
    from tools.test_only.ble_sim.protocol import (
        DEDUPE_TTL_S,
        DEDUPE_WINDOW_SIZE,
        DeduplicationCache,
        Envelope,
        FlowController,
        Fragmenter,
        LaneType,
        ProtocolError,
        Reassembler,
        create_error_response,
    )
except ImportError:
    try:
        import dashcue_pb2 as pb

        from protocol import (
            DEDUPE_TTL_S,
            DEDUPE_WINDOW_SIZE,
            DeduplicationCache,
            Envelope,
            FlowController,
            Fragmenter,
            LaneType,
            ProtocolError,
            Reassembler,
            create_error_response,
        )
    except ImportError:
        from . import dashcue_pb2 as pb
        from .protocol import (
            DEDUPE_TTL_S,
            DEDUPE_WINDOW_SIZE,
            DeduplicationCache,
            Envelope,
            FlowController,
            Fragmenter,
            LaneType,
            ProtocolError,
            Reassembler,
            create_error_response,
        )


class HudSystemState(str, Enum):
    BOOTING = "Booting"
    ADVERTISING = "Advertising"
    ADVERTISING_PAIRING = "AdvertisingPairing"
    CONNECTED_UNSYNCED = "ConnectedUnsynced"
    READY = "Ready"
    STALE = "Stale"
    SAFEMODE = "SafeMode"


class ScreenId(str, Enum):
    MEDIA = "Media"
    NAV = "Nav"
    MAP = "Map"
    SETTINGS = "Settings"
    DIAGNOSTICS = "Diagnostics"
    CALIBRATION = "Calibration"
    SAFEMODE = "SafeMode"


@dataclass
class TouchCalibrationBlob:
    """5-point affine transformation matrix and residual error metrics."""

    # Screen points (target): (x_s, y_s) = (A*x_t + B*y_t + C, D*x_t + E*y_t + F) / divisor
    a: float = 1.0
    b: float = 0.0
    c: float = 0.0
    d: float = 0.0
    e: float = 1.0
    f: float = 0.0
    divisor: float = 1.0
    mean_residual_px: float = 2.4
    max_residual_px: float = 5.1
    is_valid: bool = True
    crc32: int = 0

    def compute_crc(self) -> int:
        packed = struct.pack(
            "<fffffffff",
            self.a,
            self.b,
            self.c,
            self.d,
            self.e,
            self.f,
            self.divisor,
            self.mean_residual_px,
            self.max_residual_px,
        )
        return zlib.crc32(packed) & 0xFFFFFFFF


class MockHud:
    """Full-fidelity virtual HUD peripheral."""

    def __init__(self, mac_address: str = "D4:E9:F4:6F:F0:88"):
        self.mac_address = mac_address
        self.fw_version = "0.1.0"
        self.hw_revision = "cyd-2432s028r-r1"
        self.state = HudSystemState.BOOTING
        self.current_screen = ScreenId.MEDIA
        self.active_session_id = 0
        self.negotiated_mtu = 247

        # BLE Security & Pairing
        self.pairing_window_active = False
        self.pairing_window_expires_at = 0.0
        self.passkey: str = "123456"
        self.bonded_central_id: str | None = None
        self.authenticated = False

        # FreeRTOS IPC Queues (16 items each)
        self.ui_to_proto_queue: collections.deque[dict] = collections.deque(maxlen=16)
        self.proto_to_ui_queue: collections.deque[dict] = collections.deque(maxlen=16)
        self.queue_drops = 0

        # Protocols & Codecs
        self.reassembler = Reassembler()
        self.dedupe_cache = DeduplicationCache(capacity=DEDUPE_WINDOW_SIZE, ttl=DEDUPE_TTL_S)
        self.flow_controller = FlowController()
        self.tx_notification_queue: list[bytes] = []

        # Touch & Display Subsystems
        self.calibration = TouchCalibrationBlob()
        self.calibration.crc32 = self.calibration.compute_crc()
        self.fps: float = 28.5
        self.free_heap_bytes: int = 161280
        self.min_free_heap_bytes: int = 158000
        self.largest_free_block: int = 98000
        self.touch_hz: int = 100

        # Safety & Driving Lock
        self.reported_speed_kmh: float = 0.0
        self.driving_lock_active = False
        self.reset_history_timestamps: list[float] = []
        self.reset_count = 1
        self.uptime_start = time.time()
        self.last_heartbeat_time = time.time()
        self.stale_timeout_s = 6.0  # 3 missed heartbeats

        # Current Subsystem State
        self.media_state = pb.MediaState(
            snapshot_version=1,
            status=pb.PlaybackStatus.PB_STOPPED,
            title="No Media",
            artist="",
            album="",
            app_name="",
            capabilities=(1 << pb.MediaAction.MA_PLAY)
            | (1 << pb.MediaAction.MA_PAUSE)
            | (1 << pb.MediaAction.MA_NEXT)
            | (1 << pb.MediaAction.MA_PREV)
            | (1 << pb.MediaAction.MA_VOL_STEP),
            volume_pct=60,
        )
        self.nav_state = pb.NavState(
            status=pb.NavStatus.NAV_NONE,
            dist_to_next_m=0,
            remaining_m=0,
            remaining_s=0,
        )
        self.route_overview: pb.RouteOverview | None = None
        self.settings = pb.SettingsData(
            brightness_pct=75,
            theme=pb.ThemeMode.TH_AUTO,
            orientation=pb.ScreenOrientation.SO_LANDSCAPE,
            seek_step_s=10,
            units=pb.UnitSystem.UN_METRIC,
            version=1,
        )

        # Boot
        self.boot()

    def boot(self, now: float | None = None) -> None:
        current_time = now if now is not None else time.time()
        self.uptime_start = current_time

        # Check SafeMode trigger: >= 3 crash resets within 60s
        recent_resets = [
            t for t in self.reset_history_timestamps if 0.0 <= (current_time - t) <= 60.0
        ]
        if len(recent_resets) >= 3:
            self.state = HudSystemState.SAFEMODE
            self.current_screen = ScreenId.SAFEMODE
            return

        # Check Touch Calibration Gating
        if not self.calibration.is_valid or self.calibration.mean_residual_px > 6.0 or self.calibration.max_residual_px > 12.0:
            self.state = HudSystemState.BOOTING
            self.current_screen = ScreenId.CALIBRATION
            return

        self.state = HudSystemState.ADVERTISING
        self.current_screen = ScreenId.MEDIA

    def simulate_crash_reset(self, now: float | None = None) -> None:
        current_time = now if now is not None else time.time()
        self.reset_count += 1
        self.reset_history_timestamps.append(current_time)
        self.boot(now=now)

    def start_pairing_mode(self, now: float | None = None) -> None:
        current_time = now if now is not None else time.time()
        self.state = HudSystemState.ADVERTISING_PAIRING
        self.pairing_window_active = True
        self.pairing_window_expires_at = current_time + 120.0

    def connect(self, central_id: str, now: float | None = None) -> bool:
        current_time = now if now is not None else time.time()
        if self.state == HudSystemState.SAFEMODE:
            return False

        # If already paired and bonded to another central without accept-list match
        if self.bonded_central_id is not None and self.bonded_central_id != central_id:
            return False  # Refused: single central only

        if self.bonded_central_id == central_id:
            self.authenticated = True
        else:
            # Requires pairing mode
            if not self.pairing_window_active or current_time > self.pairing_window_expires_at:
                return False
            self.authenticated = False

        self.state = HudSystemState.CONNECTED_UNSYNCED
        self.active_session_id = (int(current_time * 1000) & 0xFFFF) or 1
        self.last_heartbeat_time = current_time
        return True

    def authenticate_passkey(self, central_id: str, entered_passkey: str) -> bool:
        if self.state != HudSystemState.CONNECTED_UNSYNCED:
            return False
        if entered_passkey == self.passkey:
            self.authenticated = True
            self.bonded_central_id = central_id
            self.pairing_window_active = False
            return True
        return False

    def exchange_mtu(self, requested_mtu: int) -> int:
        self.negotiated_mtu = max(23, min(requested_mtu, 247))
        return self.negotiated_mtu

    def disconnect(self) -> None:
        if self.state not in (HudSystemState.SAFEMODE, HudSystemState.BOOTING):
            self.state = HudSystemState.ADVERTISING
        self.authenticated = False
        self.active_session_id = 0
        self.reassembler.reset_lane(LaneType.CONTROL)
        self.reassembler.reset_lane(LaneType.BULK)

    def tick(self, now: float | None = None) -> None:
        current_time = now if now is not None else time.time()
        # Stale Link Detection (6 s without heartbeat)
        if (
            self.state == HudSystemState.READY
            and current_time - self.last_heartbeat_time > self.stale_timeout_s
        ):
            self.state = HudSystemState.STALE
            # When stale, driving lock speed is unknown (-1), released per SI-05
            self.reported_speed_kmh = -1.0
            self.driving_lock_active = False

        # Pairing timeout
        if self.pairing_window_active and current_time > self.pairing_window_expires_at:
            self.pairing_window_active = False
            if self.state == HudSystemState.ADVERTISING_PAIRING:
                self.state = HudSystemState.ADVERTISING

    def push_ui_command(self, cmd: dict) -> bool:
        """Push a touch command to proto_task via ui_to_proto_queue."""
        if len(self.ui_to_proto_queue) >= 16:
            self.queue_drops += 1
            return False
        self.ui_to_proto_queue.append(cmd)
        return True

    def pop_ui_command(self) -> dict | None:
        if self.ui_to_proto_queue:
            return self.ui_to_proto_queue.popleft()
        return None

    def push_proto_event(self, event: dict) -> bool:
        """Push a decoded proto event to ui_task via proto_to_ui_queue."""
        if len(self.proto_to_ui_queue) >= 16:
            self.queue_drops += 1
            return False
        self.proto_to_ui_queue.append(event)
        return True

    def pop_proto_event(self) -> dict | None:
        if self.proto_to_ui_queue:
            return self.proto_to_ui_queue.popleft()
        return None

    def receive_rx_fragment(
        self, fragment: bytes, lane: int = LaneType.CONTROL, now: float | None = None
    ) -> Envelope | None:
        """Receive a raw ATT fragment over GATT RX characteristic."""
        if not self.authenticated and self.state != HudSystemState.CONNECTED_UNSYNCED:
            # Unauthenticated write rejected
            return None

        current_time = now if now is not None else time.time()
        assembled = self.reassembler.push_fragment(fragment, lane=lane, now=current_time)
        if assembled is None:
            return None

        # Parse Envelope
        try:
            env = Envelope.unpack(assembled)
        except ProtocolError as e:
            # Return error notification over TX
            err_env = create_error_response(
                correlation=e.correlation,
                code=e.code,
                text=str(e),
                session_id=self.active_session_id,
            )
            self._queue_tx_notification(err_env)
            return None

        # Process decoded envelope
        self._process_envelope(env, now=current_time)
        return env

    def _process_envelope(self, env: Envelope, now: float) -> None:
        # Check session_id
        if self.state != HudSystemState.CONNECTED_UNSYNCED and env.session_id != self.active_session_id:
            err_env = create_error_response(
                correlation=env.correlation,
                code=pb.ErrorCode.EC_SESSION_MISMATCH,
                text="Session mismatch",
                session_id=self.active_session_id,
            )
            self._queue_tx_notification(err_env)
            return

        # Dispatch based on msg_type
        if env.msg_type == pb.MessageType.MT_HELLO:
            hello = pb.Hello()
            hello.ParseFromString(env.payload)
            if hello.proto_major != 1:
                ack = pb.HelloAck(
                    proto_major=1,
                    proto_minor=0,
                    accepted=0,
                    reject_reason=pb.ErrorCode.EC_VERSION_MISMATCH,
                )
            else:
                ack = pb.HelloAck(
                    proto_major=1,
                    proto_minor=min(hello.proto_minor, 0),
                    capabilities=pb.Capability.CAP_ARTWORK
                    | pb.Capability.CAP_ROUTE_OVERVIEW
                    | pb.Capability.CAP_TOUCH_CALIBRATION
                    | pb.Capability.CAP_DIAGNOSTICS
                    | pb.Capability.CAP_SEEK,
                    fw_version=self.fw_version,
                    hw_revision=self.hw_revision,
                    max_msg_size=4096,
                    mtu=self.negotiated_mtu,
                    accepted=1,
                )
                self.state = HudSystemState.READY
            resp_env = Envelope.create(
                msg_type=pb.MessageType.MT_HELLO_ACK,
                payload=ack.SerializeToString(),
                session_id=self.active_session_id,
                correlation=env.seq,
                flags=pb.EnvelopeFlag.EF_IS_RESPONSE,
            )
            self._queue_tx_notification(resp_env)

        elif env.msg_type == pb.MessageType.MT_SNAPSHOT_REQUEST:
            # Respond with DeviceStatus and SettingsState
            status = pb.DeviceStatus(
                uptime_s=int(now - self.uptime_start),
                reset_count=self.reset_count,
                min_free_heap=self.min_free_heap_bytes,
                largest_free_block=self.largest_free_block,
                queue_drops=self.queue_drops,
                crc_failures=0,
                frag_timeouts=self.reassembler.frag_timeouts,
                safe_mode=(self.state == HudSystemState.SAFEMODE),
            )
            self._queue_tx_notification(
                Envelope.create(
                    msg_type=pb.MessageType.MT_DEVICE_STATUS,
                    payload=status.SerializeToString(),
                    session_id=self.active_session_id,
                    flags=pb.EnvelopeFlag.EF_IS_RESPONSE,
                )
            )

            settings_state = pb.SettingsState(
                settings=self.settings,
                confirmed=True,
            )
            self._queue_tx_notification(
                Envelope.create(
                    msg_type=pb.MessageType.MT_SETTINGS_STATE,
                    payload=settings_state.SerializeToString(),
                    session_id=self.active_session_id,
                    flags=pb.EnvelopeFlag.EF_IS_RESPONSE,
                )
            )

        elif env.msg_type == pb.MessageType.MT_MEDIA_CMD:
            cmd = pb.MediaCmd()
            cmd.ParseFromString(env.payload)

            # Check dedupe cache
            cached_result = self.dedupe_cache.lookup(cmd.command_id, now=now)
            if cached_result is not None:
                # Replay cached result immediately
                res_env = Envelope.create(
                    msg_type=pb.MessageType.MT_CMD_RESULT,
                    payload=cached_result.SerializeToString(),
                    session_id=self.active_session_id,
                    correlation=cmd.command_id,
                    flags=pb.EnvelopeFlag.EF_IS_RESPONSE,
                )
                self._queue_tx_notification(res_env)
                return

            # Execute command
            result_status = pb.CmdResult.Status.ST_CONFIRMED
            reason = pb.ErrorCode.EC_NONE

            if cmd.action == pb.MediaAction.MA_PLAY:
                self.media_state.status = pb.PlaybackStatus.PB_PLAYING
            elif cmd.action == pb.MediaAction.MA_PAUSE:
                self.media_state.status = pb.PlaybackStatus.PB_PAUSED
            elif cmd.action == pb.MediaAction.MA_NEXT or cmd.action == pb.MediaAction.MA_PREV:
                self.media_state.snapshot_version += 1
            elif cmd.action == pb.MediaAction.MA_VOL_SET:
                self.media_state.volume_pct = max(0, min(100, int(cmd.arg)))

            res = pb.CmdResult(
                command_id=cmd.command_id,
                status=result_status,
                reason=reason,
                observed_at_ms=int(now * 1000),
            )
            self.dedupe_cache.store(cmd.command_id, res, now=now)

            res_env = Envelope.create(
                msg_type=pb.MessageType.MT_CMD_RESULT,
                payload=res.SerializeToString(),
                session_id=self.active_session_id,
                correlation=cmd.command_id,
                flags=pb.EnvelopeFlag.EF_IS_RESPONSE,
            )
            self._queue_tx_notification(res_env)

        elif env.msg_type == pb.MessageType.MT_MEDIA_STATE:
            state = pb.MediaState()
            state.ParseFromString(env.payload)
            self.media_state = state

        elif env.msg_type == pb.MessageType.MT_NAV_STATE:
            nav = pb.NavState()
            nav.ParseFromString(env.payload)
            self.nav_state = nav
            if nav.speed_kmh_x10 >= 0:
                self.reported_speed_kmh = nav.speed_kmh_x10 / 10.0
                self.driving_lock_active = self.reported_speed_kmh >= 8.0
                if self.driving_lock_active and self.current_screen == ScreenId.SETTINGS:
                    self.current_screen = ScreenId.NAV

        elif env.msg_type == pb.MessageType.MT_ROUTE_OVERVIEW:
            route = pb.RouteOverview()
            route.ParseFromString(env.payload)
            self.route_overview = route

        elif env.msg_type == pb.MessageType.MT_SETTINGS_SET:
            s_set = pb.SettingsSet()
            s_set.ParseFromString(env.payload)
            if self.driving_lock_active:
                # Reject mutating settings while driving
                err_env = create_error_response(
                    correlation=s_set.command_id,
                    code=pb.ErrorCode.EC_BUSY,
                    text="Driving lock active: settings restricted",
                    session_id=self.active_session_id,
                )
                self._queue_tx_notification(err_env)
                return

            self.settings = s_set.settings
            self.settings.brightness_pct = max(10, min(100, self.settings.brightness_pct))
            state_resp = pb.SettingsState(settings=self.settings, confirmed=True)
            self._queue_tx_notification(
                Envelope.create(
                    msg_type=pb.MessageType.MT_SETTINGS_STATE,
                    payload=state_resp.SerializeToString(),
                    session_id=self.active_session_id,
                    correlation=s_set.command_id,
                    flags=pb.EnvelopeFlag.EF_IS_RESPONSE,
                )
            )

        elif env.msg_type == pb.MessageType.MT_HEARTBEAT:
            hb = pb.Heartbeat()
            hb.ParseFromString(env.payload)
            self.last_heartbeat_time = now
            if self.state == HudSystemState.STALE:
                self.state = HudSystemState.READY

            if hb.speed_kmh_x10 >= 0:
                self.reported_speed_kmh = hb.speed_kmh_x10 / 10.0
                self.driving_lock_active = self.reported_speed_kmh >= 8.0
                if self.driving_lock_active and self.current_screen == ScreenId.SETTINGS:
                    self.current_screen = ScreenId.MEDIA
            else:
                self.reported_speed_kmh = -1.0
                self.driving_lock_active = False

            # Echo heartbeat with granted credits
            reply_hb = pb.Heartbeat(
                phone_ts_ms=int(now * 1000),
                credits=8,
                speed_kmh_x10=int(self.reported_speed_kmh * 10) if self.reported_speed_kmh >= 0 else -1,
                seq=hb.seq,
            )
            self._queue_tx_notification(
                Envelope.create(
                    msg_type=pb.MessageType.MT_HEARTBEAT,
                    payload=reply_hb.SerializeToString(),
                    session_id=self.active_session_id,
                    correlation=hb.seq,
                )
            )

        elif env.msg_type == pb.MessageType.MT_DIAG_REQ:
            diag = pb.DiagReq()
            diag.ParseFromString(env.payload)
            status = pb.DeviceStatus(
                uptime_s=int(now - self.uptime_start),
                reset_count=self.reset_count,
                min_free_heap=self.min_free_heap_bytes,
                largest_free_block=self.largest_free_block,
                queue_drops=self.queue_drops,
                crc_failures=0,
                frag_timeouts=self.reassembler.frag_timeouts,
                safe_mode=(self.state == HudSystemState.SAFEMODE),
                touch_hz=self.touch_hz,
            )
            resp = pb.DiagResp(
                request_seq=diag.command_id,
                status=status,
                log_lines=["[SYS] Boot complete", "[BLE] Connected", f"[LINK] MTU={self.negotiated_mtu}"],
                log_total_lines=3,
            )
            self._queue_tx_notification(
                Envelope.create(
                    msg_type=pb.MessageType.MT_DIAG_RESP,
                    payload=resp.SerializeToString(),
                    session_id=self.active_session_id,
                    correlation=diag.command_id,
                    flags=pb.EnvelopeFlag.EF_IS_RESPONSE,
                )
            )

    def _queue_tx_notification(self, env: Envelope) -> None:
        packed_envelope = env.pack()
        fragments = Fragmenter.fragment(env.seq & 0xFFFF, packed_envelope, mtu=self.negotiated_mtu)
        self.tx_notification_queue.extend(fragments)

    def pop_tx_notification(self) -> bytes | None:
        if self.tx_notification_queue:
            return self.tx_notification_queue.pop(0)
        return None

    def switch_screen(self, screen: ScreenId) -> bool:
        if self.driving_lock_active and screen == ScreenId.SETTINGS:
            return False  # Locked while driving
        self.current_screen = screen
        return True
