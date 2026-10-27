"""In-process BLE central model for DashCue host simulation tests.

Uses MockHud only. No radio, physical device, Android service, encryption, or
actual passkey authentication is exercised by this module.

Simulates phone central GATT client:
- State transitions: Disconnected -> Scanning -> Connecting -> DiscoveringServices -> Negotiating -> Synchronising -> Ready
- Auto-reconnection exponential backoff (1s, 2s, 4s, 8s, 15s, 30s +-20% jitter)
- LE Secure Connections / passkey pairing
- ATT MTU exchange (23..247)
- Framed and fragmented RX writes with flow control credit monitoring
- Subscribing to TX notifications and reassembly
- Command deduplication & retransmission verification
"""

from __future__ import annotations

import argparse
import random
import sys
import time
from enum import Enum

try:
    from tools.test_only.ble_sim import dashcue_pb2 as pb
    from tools.test_only.ble_sim.mock_hud import MockHud
    from tools.test_only.ble_sim.protocol import (
        Envelope,
        FlowController,
        Fragmenter,
        LaneType,
        ProtocolError,
        Reassembler,
    )
except ImportError:
    try:
        import dashcue_pb2 as pb
        from mock_hud import MockHud

        from protocol import (
            Envelope,
            FlowController,
            Fragmenter,
            LaneType,
            ProtocolError,
            Reassembler,
        )
    except ImportError:
        from . import dashcue_pb2 as pb
        from .mock_hud import MockHud
        from .protocol import (
            Envelope,
            FlowController,
            Fragmenter,
            LaneType,
            ProtocolError,
            Reassembler,
        )


class CentralState(str, Enum):
    DISCONNECTED = "Disconnected"
    SCANNING = "Scanning"
    CONNECTING = "Connecting"
    DISCOVERING_SERVICES = "DiscoveringServices"
    NEGOTIATING = "Negotiating"
    SYNCHRONISING = "Synchronising"
    READY = "Ready"
    BACKOFF = "Backoff"
    ERROR_BLUETOOTH_OFF = "ErrorBluetoothOff"
    ERROR_PERMISSION = "ErrorPermission"
    ERROR_AUTH = "ErrorAuth"
    ERROR_VERSION_MISMATCH = "ErrorVersionMismatch"


BACKOFF_LADDER = [1.0, 2.0, 4.0, 8.0, 15.0, 30.0]


class BleCentralSim:
    """Simulates Android Companion BLE Central Client."""

    def __init__(
        self,
        hud: MockHud | None = None,
        client_id: str = "phone-cph2585",
        mtu: int = 247,
    ):
        self.hud = hud if hud is not None else MockHud()
        self.client_id = client_id
        self.desired_mtu = mtu
        self.effective_mtu = 23
        self.state = CentralState.DISCONNECTED
        self.session_id = 0
        self.seq = 1

        # Reconnection & Backoff
        self.backoff_step = 0
        self.ready_since: float | None = None
        self.backoff_until: float = 0.0

        # Flow Control & Reassembly
        self.flow_controller = FlowController()
        self.reassembler = Reassembler()
        self.received_envelopes: list[Envelope] = []
        self.last_received_cmd_result: pb.CmdResult | None = None
        self.last_received_heartbeat: pb.Heartbeat | None = None
        self.last_error: pb.ErrorMsg | None = None

    def calculate_backoff(self, now: float | None = None) -> float:
        """Computes exponential backoff with +-20% jitter."""
        base = BACKOFF_LADDER[min(self.backoff_step, len(BACKOFF_LADDER) - 1)]
        jitter = random.uniform(-0.20, 0.20) * base
        duration = max(0.5, base + jitter)
        current_time = now if now is not None else time.time()
        self.backoff_until = current_time + duration
        self.backoff_step = min(self.backoff_step + 1, len(BACKOFF_LADDER) - 1)
        return duration

    def reset_backoff(self) -> None:
        self.backoff_step = 0
        self.backoff_until = 0.0

    def start_connection(
        self,
        passkey: str | None = "123456",
        proto_major: int = 1,
        proto_minor: int = 0,
        now: float | None = None,
    ) -> bool:
        """Executes full connection sequence up to READY state."""
        current_time = now if now is not None else time.time()
        self.state = CentralState.SCANNING

        # Connect to peripheral
        self.state = CentralState.CONNECTING
        connected = self.hud.connect(self.client_id, now=current_time)
        if not connected:
            self.state = CentralState.BACKOFF
            self.calculate_backoff(now=current_time)
            return False

        # Discover GATT services
        self.state = CentralState.DISCOVERING_SERVICES

        # LE Security passkey pairing if needed
        if not self.hud.authenticated and (
            passkey is None or not self.hud.authenticate_passkey(self.client_id, passkey)
        ):
            self.state = CentralState.ERROR_AUTH
            self.hud.disconnect()
            return False

        # Negotiate MTU
        self.state = CentralState.NEGOTIATING
        self.effective_mtu = self.hud.exchange_mtu(self.desired_mtu)

        # Send HELLO
        hello = pb.Hello(
            proto_major=proto_major,
            proto_minor=proto_minor,
            capabilities=pb.Capability.CAP_ARTWORK
            | pb.Capability.CAP_ROUTE_OVERVIEW
            | pb.Capability.CAP_TOUCH_CALIBRATION
            | pb.Capability.CAP_DIAGNOSTICS
            | pb.Capability.CAP_SEEK,
            device_id=self.client_id,
            artwork_size=96,
            max_msg_size=4096,
            mtu=self.effective_mtu,
        )
        self.session_id = self.hud.active_session_id
        hello_env = Envelope.create(
            msg_type=pb.MessageType.MT_HELLO,
            payload=hello.SerializeToString(),
            session_id=self.session_id,
            seq=self._next_seq(),
        )
        self.send_envelope(hello_env, now=current_time)
        self.poll_notifications(now=current_time)

        # Check for HELLO_ACK
        ack_env = self.find_latest_envelope(pb.MessageType.MT_HELLO_ACK)
        if ack_env is None:
            self.state = CentralState.BACKOFF
            return False

        ack = pb.HelloAck()
        ack.ParseFromString(ack_env.payload)
        if ack.accepted != 1:
            if ack.reject_reason == pb.ErrorCode.EC_VERSION_MISMATCH:
                self.state = CentralState.ERROR_VERSION_MISMATCH
            else:
                self.state = CentralState.BACKOFF
            self.hud.disconnect()
            return False

        # Synchronising state snapshot
        self.state = CentralState.SYNCHRONISING
        snap_req = pb.SnapshotRequest(
            request_seq=self._next_seq(),
            want_media=15,  # all
        )
        snap_env = Envelope.create(
            msg_type=pb.MessageType.MT_SNAPSHOT_REQUEST,
            payload=snap_req.SerializeToString(),
            session_id=self.session_id,
            seq=self._next_seq(),
        )
        self.send_envelope(snap_env, now=current_time)
        self.poll_notifications(now=current_time)

        self.state = CentralState.READY
        self.ready_since = current_time
        return True

    def disconnect(self) -> None:
        self.hud.disconnect()
        self.state = CentralState.DISCONNECTED
        self.session_id = 0
        self.ready_since = None

    def poll_notifications(self, now: float | None = None) -> list[Envelope]:
        """Drains HUD TX notifications and reassembles incoming envelopes."""
        current_time = now if now is not None else time.time()
        new_envelopes: list[Envelope] = []
        while True:
            raw_frag = self.hud.pop_tx_notification()
            if raw_frag is None:
                break
            assembled = self.reassembler.push_fragment(raw_frag, now=current_time)
            if assembled is not None:
                try:
                    env = Envelope.unpack(assembled)
                    self.received_envelopes.append(env)
                    new_envelopes.append(env)
                    self._dispatch_incoming(env)
                except ProtocolError:
                    pass
        return new_envelopes

    def _dispatch_incoming(self, env: Envelope) -> None:
        if env.msg_type == pb.MessageType.MT_CMD_RESULT:
            res = pb.CmdResult()
            res.ParseFromString(env.payload)
            self.last_received_cmd_result = res
        elif env.msg_type == pb.MessageType.MT_HEARTBEAT:
            hb = pb.Heartbeat()
            hb.ParseFromString(env.payload)
            self.last_received_heartbeat = hb
            self.flow_controller.grant_credits(hb.credits)
        elif env.msg_type == pb.MessageType.MT_ERROR:
            err = pb.ErrorMsg()
            err.ParseFromString(env.payload)
            self.last_error = err

    def send_envelope(
        self,
        env: Envelope,
        lane: int = LaneType.CONTROL,
        now: float | None = None,
    ) -> bool:
        """Fragments envelope and delivers it to HUD RX characteristic."""
        if not self.flow_controller.can_send(lane):
            return False

        packed = env.pack()
        fragments = Fragmenter.fragment(env.seq & 0xFFFF, packed, mtu=self.effective_mtu)
        current_time = now if now is not None else time.time()
        for frag in fragments:
            self.flow_controller.consume(lane)
            self.hud.receive_rx_fragment(frag, lane=lane, now=current_time)
        return True

    def send_media_cmd(
        self,
        command_id: int,
        action: int,
        arg: int = 0,
        now: float | None = None,
    ) -> pb.CmdResult | None:
        """Sends a MediaCmd with 1.0s retransmission retry per PROTOCOL.md §5."""
        current_time = now if now is not None else time.time()
        cmd = pb.MediaCmd(
            command_id=command_id,
            action=action,
            arg=arg,
            snapshot_version=self.hud.media_state.snapshot_version,
        )
        env = Envelope.create(
            msg_type=pb.MessageType.MT_MEDIA_CMD,
            payload=cmd.SerializeToString(),
            session_id=self.session_id,
            seq=self._next_seq(),
            correlation=command_id,
            flags=pb.EnvelopeFlag.EF_ACK_REQUIRED,
        )

        # Transmission with up to 2 retries on same command_id
        for attempt in range(3):
            self.send_envelope(env, lane=LaneType.CONTROL, now=current_time)
            self.poll_notifications(now=current_time)
            if (
                self.last_received_cmd_result is not None
                and self.last_received_cmd_result.command_id == command_id
            ):
                return self.last_received_cmd_result
            current_time += 1.0  # Simulated 1s retransmit delay

        return self.last_received_cmd_result

    def send_heartbeat(
        self,
        speed_kmh: float = -1.0,
        credits: int = 8,
        now: float | None = None,
    ) -> None:
        current_time = now if now is not None else time.time()
        hb = pb.Heartbeat(
            phone_ts_ms=int(current_time * 1000),
            credits=credits,
            speed_kmh_x10=int(speed_kmh * 10) if speed_kmh >= 0 else -1,
            seq=self._next_seq(),
        )
        env = Envelope.create(
            msg_type=pb.MessageType.MT_HEARTBEAT,
            payload=hb.SerializeToString(),
            session_id=self.session_id,
            seq=self._next_seq(),
        )
        self.send_envelope(env, lane=LaneType.CONTROL, now=current_time)
        self.poll_notifications(now=current_time)

    def find_latest_envelope(self, msg_type: int) -> Envelope | None:
        for env in reversed(self.received_envelopes):
            if env.msg_type == msg_type:
                return env
        return None

    def _next_seq(self) -> int:
        s = self.seq
        self.seq += 1
        return s


def main() -> int:
    parser = argparse.ArgumentParser(description="In-process MockHud simulation; no Bluetooth hardware support")
    parser.add_argument("--client-id", default="phone-cph2585", help="Client device ID")
    parser.add_argument("--mtu", type=int, default=247, help="Requested ATT MTU")
    parser.add_argument("--passkey", default="123456", help="Pairing passkey")
    args = parser.parse_args()

    print(f"[SIM] Starting in-process MockHud model (Client: {args.client_id}, MTU: {args.mtu}); no hardware")
    hud = MockHud()
    hud.start_pairing_mode()
    sim = BleCentralSim(hud=hud, client_id=args.client_id, mtu=args.mtu)

    print("[SIM] Connecting...")
    ok = sim.start_connection(passkey=args.passkey)
    if not ok:
        print(f"[SIM] Connection failed: state={sim.state}")
        return 1

    print(f"[SIM] Connected and Synchronised! HUD state={hud.state}, Negotiated MTU={sim.effective_mtu}")
    print("[SIM] Sending PLAY command...")
    res = sim.send_media_cmd(command_id=101, action=pb.MediaAction.MA_PLAY)
    print(f"[SIM] Command Result: status={res.status if res else 'None'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
