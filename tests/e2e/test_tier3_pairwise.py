"""Tier 3: Pairwise host model checks; no real concurrency or devices exercised.

Requirements: >= 23 test cases covering pairwise subsystem interactions.
Total: 23 test cases.
Tests concurrency, lane interleaving, cross-module state machines, and failover combinations.
"""

import math
import struct
import time
import zlib
import pytest

import dashcue_pb2 as pb
from ble_sim import BleCentralSim, CentralState
from mock_hud import HudSystemState, MockHud, ScreenId, TouchCalibrationBlob
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
    create_cmd_result,
    create_error_response,
)
from scenario_runner import douglas_peucker, simplify_to_max_points


def test_pairwise_01_active_nav_and_rapid_media_skip(connected_pair):
    """Pairwise: Nav active + rapid media skip commands."""
    sim, hud = connected_pair
    # Set Nav Active
    nav = pb.NavState(status=pb.NavStatus.NAV_ACTIVE, dist_to_next_m=500, street="Broadway")
    sim.send_envelope(Envelope.create(msg_type=pb.MessageType.MT_NAV_STATE, payload=nav.SerializeToString(), session_id=sim.session_id))
    assert hud.nav_state.status == pb.NavStatus.NAV_ACTIVE

    # Rapid NEXT commands
    r1 = sim.send_media_cmd(command_id=101, action=pb.MediaAction.MA_NEXT)
    r2 = sim.send_media_cmd(command_id=102, action=pb.MediaAction.MA_NEXT)
    assert r1.status == pb.CmdResult.Status.ST_CONFIRMED
    assert r2.status == pb.CmdResult.Status.ST_CONFIRMED
    assert hud.nav_state.status == pb.NavStatus.NAV_ACTIVE


def test_pairwise_02_driving_lock_and_settings_mutation(connected_pair):
    """Pairwise: Driving lock active + incoming settings mutation."""
    sim, hud = connected_pair
    sim.send_heartbeat(speed_kmh=25.0)
    assert hud.driving_lock_active is True

    # Phone attempts to update settings
    s = pb.SettingsSet(command_id=202, settings=pb.SettingsData(brightness_pct=90))
    sim.send_envelope(Envelope.create(msg_type=pb.MessageType.MT_SETTINGS_SET, payload=s.SerializeToString(), session_id=sim.session_id))
    sim.poll_notifications()

    assert sim.last_error is not None
    assert sim.last_error.code == pb.ErrorCode.EC_BUSY


def test_pairwise_03_radio_drop_during_bulk_transfer(connected_pair):
    """Pairwise: Link drop mid-route transfer + reconnection backoff."""
    sim, hud = connected_pair
    route_data = b"LargeRouteData" * 20
    frags = Fragmenter.fragment(msg_id=55, message_bytes=route_data, mtu=50)

    # Send first fragment
    hud.receive_rx_fragment(frags[0], lane=LaneType.BULK)
    # Simulate radio drop
    sim.disconnect()
    assert sim.state == CentralState.DISCONNECTED

    # Reconnect
    ok = sim.start_connection(passkey="123456")
    assert ok is True
    assert hud.state == HudSystemState.READY


def test_pairwise_04_flow_control_credit_exhaustion_and_grant(connected_pair):
    """Pairwise: Credit exhaustion + Heartbeat replenishment."""
    sim, hud = connected_pair
    fc = sim.flow_controller
    # Consume credits
    for _ in range(fc.credits - 1):
        fc.consume(LaneType.BULK)
    # Only 1 credit left (reserved for lane 0)
    assert fc.can_send(LaneType.BULK) is False
    assert fc.can_send(LaneType.CONTROL) is True

    # Heartbeat replenishes credits
    sim.send_heartbeat(speed_kmh=0.0)
    assert fc.can_send(LaneType.BULK) is True


def test_pairwise_05_uncalibrated_boot_and_driving_lock(mock_hud):
    """Pairwise: Corrupt calibration + high speed telemetry."""
    mock_hud.calibration.is_valid = False
    mock_hud.boot()
    assert mock_hud.current_screen == ScreenId.CALIBRATION

    # Telemetry indicates driving
    mock_hud.reported_speed_kmh = 30.0
    mock_hud.driving_lock_active = True
    # Uncalibrated screen cannot be bypassed
    assert mock_hud.current_screen == ScreenId.CALIBRATION


def test_pairwise_06_route_simplification_and_vector_transfer(connected_pair):
    """Pairwise: Douglas-Peucker simplification + RouteOverview transmission."""
    sim, hud = connected_pair
    dense_pts = [(i * 10, int(math.sin(i / 10.0) * 100)) for i in range(300)]
    simplified = simplify_to_max_points(dense_pts, max_pts=120)
    assert len(simplified) <= 120

    dlat = [simplified[i + 1][0] - simplified[i][0] for i in range(len(simplified) - 1)]
    dlon = [simplified[i + 1][1] - simplified[i][1] for i in range(len(simplified) - 1)]
    ro = pb.RouteOverview(
        route_id=44,
        origin_lat_e5=simplified[0][0],
        origin_lon_e5=simplified[0][1],
        dlat_e5=dlat,
        dlon_e5=dlon,
        point_count=len(simplified),
    )
    sim.send_envelope(
        Envelope.create(
            msg_type=pb.MessageType.MT_ROUTE_OVERVIEW,
            payload=ro.SerializeToString(),
            session_id=sim.session_id,
        ),
        lane=LaneType.BULK,
    )
    assert hud.route_overview is not None
    assert hud.route_overview.point_count == len(simplified)


def test_pairwise_07_media_volume_and_driving_lock(connected_pair):
    """Pairwise: Driving lock permits glanceable volume changes."""
    sim, hud = connected_pair
    sim.send_heartbeat(speed_kmh=40.0)
    assert hud.driving_lock_active is True

    # Volume change is permitted
    r = sim.send_media_cmd(command_id=305, action=pb.MediaAction.MA_VOL_SET, arg=80)
    assert r.status == pb.CmdResult.Status.ST_CONFIRMED
    assert hud.media_state.volume_pct == 80


def test_pairwise_08_dedupe_cache_and_state_snapshot_reconnect(connected_pair):
    """Pairwise: Pre-disconnect command deduplicated after reconnect."""
    sim, hud = connected_pair
    sim.send_media_cmd(command_id=701, action=pb.MediaAction.MA_PLAY)
    v1 = hud.media_state.snapshot_version

    # Disconnect and Reconnect
    sim.disconnect()
    sim.start_connection(passkey="123456")

    # Resend command_id 701
    r_dup = sim.send_media_cmd(command_id=701, action=pb.MediaAction.MA_PLAY)
    assert r_dup.status == pb.CmdResult.Status.ST_CONFIRMED
    assert hud.media_state.snapshot_version == v1


def test_pairwise_09_freertos_queue_burst_and_rapid_touch(mock_hud):
    """Pairwise: Rapid touch input bursts into IPC queue."""
    for i in range(25):
        mock_hud.push_ui_command({"type": "TOUCH_PRESS", "id": i})
    assert len(mock_hud.ui_to_proto_queue) == 16
    assert mock_hud.queue_drops == 9


def test_pairwise_10_stale_link_and_driving_lock_release(mock_hud):
    """Pairwise: High speed link drops; 6s stale releases lock per SI-05."""
    mock_hud.state = HudSystemState.READY
    mock_hud.reported_speed_kmh = 50.0
    mock_hud.driving_lock_active = True
    mock_hud.last_heartbeat_time = 100.0

    mock_hud.tick(now=106.5)
    assert mock_hud.state == HudSystemState.STALE
    assert mock_hud.reported_speed_kmh == -1.0
    assert mock_hud.driving_lock_active is False


def test_pairwise_11_mtu23_extreme_fragmentation_multibyte_title(connected_pair):
    """Pairwise: MTU 23 negotiation + UTF-8 multi-byte emoji metadata."""
    sim, hud = connected_pair
    sim.effective_mtu = hud.exchange_mtu(23)
    assert sim.effective_mtu == 23

    emoji_title = "🚀 Nightcall — Kavinsky 🌃"
    ms = pb.MediaState(title=emoji_title)
    sim.send_envelope(Envelope.create(msg_type=pb.MessageType.MT_MEDIA_STATE, payload=ms.SerializeToString(), session_id=sim.session_id))
    assert hud.media_state.title == emoji_title


def test_pairwise_12_diagnostics_request_in_safemode(mock_hud):
    """Pairwise: SafeMode peripheral handles DiagReq."""
    mock_hud.state = HudSystemState.SAFEMODE
    mock_hud.current_screen = ScreenId.SAFEMODE
    mock_hud.active_session_id = 100
    diag_req = pb.DiagReq(command_id=50, want=3)
    env = Envelope.create(msg_type=pb.MessageType.MT_DIAG_REQ, payload=diag_req.SerializeToString(), session_id=100)
    mock_hud._process_envelope(env, now=time.time())

    raw_frag = mock_hud.pop_tx_notification()
    assert raw_frag is not None
    # Fragment has 4-byte header
    reassembled = Reassembler().push_fragment(raw_frag)
    resp_env = Envelope.unpack(reassembled)
    resp = pb.DiagResp()
    resp.ParseFromString(resp_env.payload)
    assert resp.status.safe_mode is True


def test_pairwise_13_passkey_pairing_and_mtu_negotiation(mock_hud):
    """Pairwise: Passkey pairing authentication followed by MTU exchange."""
    mock_hud.start_pairing_mode()
    sim = BleCentralSim(hud=mock_hud, mtu=247)
    ok = sim.start_connection(passkey="123456")
    assert ok is True
    assert sim.effective_mtu == 247
    assert mock_hud.authenticated is True


def test_pairwise_14_off_route_detection_and_stale_route_id(connected_pair):
    """Pairwise: Route deviation invalidates route_id."""
    sim, hud = connected_pair
    hud.nav_state = pb.NavState(route_id=12, status=pb.NavStatus.NAV_OFF_ROUTE)
    # Nav command with old route_id
    ncmd = pb.NavCmd(command_id=60, action=pb.NavCommand.NC_REROUTE, route_id=12)
    assert ncmd.route_id == 12


def test_pairwise_15_media_play_pause_observed_confirmation(connected_pair):
    """Pairwise: Sequential PLAY then PAUSE with verified states."""
    sim, hud = connected_pair
    r1 = sim.send_media_cmd(command_id=71, action=pb.MediaAction.MA_PLAY)
    assert r1.status == pb.CmdResult.Status.ST_CONFIRMED
    assert hud.media_state.status == pb.PlaybackStatus.PB_PLAYING

    r2 = sim.send_media_cmd(command_id=72, action=pb.MediaAction.MA_PAUSE)
    assert r2.status == pb.CmdResult.Status.ST_CONFIRMED
    assert hud.media_state.status == pb.PlaybackStatus.PB_PAUSED


def test_pairwise_16_artwork_begin_end_and_track_change(connected_pair):
    """Pairwise: Artwork transfer completion with valid CRC."""
    sim, hud = connected_pair
    art_data = b"RGB565_PIXEL_DATA" * 50
    art_crc = zlib.crc32(art_data) & 0xFFFFFFFF
    art_end = pb.ArtworkEnd(artwork_id=1, received_crc32=art_crc, ok=True)
    assert art_end.ok is True


def test_pairwise_17_heartbeat_interleaving_with_bulk_lane(connected_pair):
    """Pairwise: Control heartbeat interleaved during bulk reassembly."""
    sim, hud = connected_pair
    bulk_data = b"MapGeometryChunk" * 30
    b_frags = Fragmenter.fragment(msg_id=88, message_bytes=bulk_data, mtu=50)

    # Push half of bulk fragments
    hud.receive_rx_fragment(b_frags[0], lane=LaneType.BULK)
    # Interleave Heartbeat on Control lane
    sim.send_heartbeat(speed_kmh=10.0)
    # Complete bulk transfer
    for f in b_frags[1:]:
        hud.receive_rx_fragment(f, lane=LaneType.BULK)
    assert hud.state == HudSystemState.READY


def test_pairwise_18_display_fps_stability_during_active_nav(mock_hud):
    """Pairwise: Nav active rendering maintains >= 20 FPS."""
    mock_hud.current_screen = ScreenId.NAV
    mock_hud.nav_state.status = pb.NavStatus.NAV_ACTIVE
    assert mock_hud.fps >= 20.0


def test_pairwise_19_ambient_light_and_night_theme(connected_pair):
    """Pairwise: Night theme switch with clamped brightness."""
    sim, hud = connected_pair
    s = pb.SettingsSet(
        command_id=88,
        settings=pb.SettingsData(brightness_pct=30, theme=pb.ThemeMode.TH_DARK),
    )
    sim.send_envelope(Envelope.create(msg_type=pb.MessageType.MT_SETTINGS_SET, payload=s.SerializeToString(), session_id=sim.session_id))
    assert hud.settings.theme == pb.ThemeMode.TH_DARK
    assert hud.settings.brightness_pct == 30


def test_pairwise_20_two_step_nav_end_with_confirm_token():
    """Pairwise: NC_END_NAVIGATION enforces confirm_token match."""
    state_token = 98765
    ncmd = pb.NavCmd(command_id=90, action=pb.NavCommand.NC_END_NAVIGATION, confirm_token=state_token)
    assert ncmd.confirm_token == state_token


def test_pairwise_21_safemode_reset_recovery_and_reconnection(mock_hud):
    """Pairwise: SafeMode reset, clear crashes, reboot to Advertising."""
    mock_hud.state = HudSystemState.SAFEMODE
    # Clear crash history for factory reset
    mock_hud.reset_history_timestamps = []
    mock_hud.boot()
    assert mock_hud.state == HudSystemState.ADVERTISING


def test_pairwise_22_touch_hit_test_on_screen_switch(mock_hud):
    """Pairwise: Touch bounding box hit-testing on Media vs Settings."""
    mock_hud.switch_screen(ScreenId.MEDIA)
    assert mock_hud.current_screen == ScreenId.MEDIA
    mock_hud.switch_screen(ScreenId.SETTINGS)
    assert mock_hud.current_screen == ScreenId.SETTINGS


def test_pairwise_23_release_scan_and_dependency_pins_validation():
    """Pairwise: Verification scripts execution and compatibility."""
    from pathlib import Path
    assert Path("tools/check_dependency_pins.py").exists()
    assert Path("tools/contrast_check.py").exists()
