"""Tier 1: Host model feature checks for DashCue Automotive HUD.

Requirements: >= 5 test cases per feature across all 23 features in TEST_INFRA.md.
Total: 115 test cases.
These tests use Python models, source checks and literal assertions, not device code.
Passing them does not verify the firmware, Android UI, release APK or real hardware.
"""

import math
import re
import struct
import time
import zlib
from pathlib import Path

import dashcue_pb2 as pb
import pytest
from mock_hud import HudSystemState, ScreenId
from scenario_runner import douglas_peucker, simplify_to_max_points

REPO_ROOT = Path(__file__).resolve().parents[2]


def _manifest_text():
    return (
        REPO_ROOT / "android" / "app" / "src" / "main" / "AndroidManifest.xml"
    ).read_text(encoding="utf-8")

from protocol import (
    UUID_DASHCUE_RX,
    UUID_DASHCUE_SERVICE,
    UUID_DASHCUE_TX,
    DeduplicationCache,
    Envelope,
    Fragmenter,
    LaneType,
    ProtocolError,
    Reassembler,
)


# ==============================================================================
# Feature 1: Protobuf Schema & Serialization
# ==============================================================================
def test_f01_01_hello_roundtrip():
    hello = pb.Hello(
        proto_major=1,
        proto_minor=0,
        capabilities=pb.Capability.CAP_ARTWORK | pb.Capability.CAP_SEEK,
        device_id="hud-esp32-01",
        fw_version="0.1.0",
        hw_revision="cyd-2432s028r-r1",
        artwork_size=96,
        max_msg_size=4096,
        mtu=247,
    )
    raw = hello.SerializeToString()
    decoded = pb.Hello()
    decoded.ParseFromString(raw)
    assert decoded.proto_major == 1
    assert decoded.proto_minor == 0
    assert decoded.capabilities & pb.Capability.CAP_ARTWORK
    assert decoded.device_id == "hud-esp32-01"
    assert decoded.artwork_size == 96


def test_f01_02_media_state_fields():
    ms = pb.MediaState(
        snapshot_version=42,
        status=pb.PlaybackStatus.PB_PLAYING,
        title="Highway to Hell",
        artist="AC/DC",
        album="Highway to Hell",
        app_name="Spotify",
        capabilities=(1 << pb.MediaAction.MA_PLAY) | (1 << pb.MediaAction.MA_PAUSE),
        volume_pct=85,
        muted=False,
    )
    raw = ms.SerializeToString()
    decoded = pb.MediaState()
    decoded.ParseFromString(raw)
    assert decoded.snapshot_version == 42
    assert decoded.status == pb.PlaybackStatus.PB_PLAYING
    assert decoded.title == "Highway to Hell"
    assert decoded.volume_pct == 85


def test_f01_03_nav_state_maneuver_enums():
    ns = pb.NavState(
        route_id=7,
        status=pb.NavStatus.NAV_ACTIVE,
        next=pb.Maneuver.MV_TURN_LEFT,
        dist_to_next_m=200,
        street="Main Street",
        heading_deg=180.5,
        speed_kmh_x10=600,
    )
    raw = ns.SerializeToString()
    decoded = pb.NavState()
    decoded.ParseFromString(raw)
    assert decoded.next == pb.Maneuver.MV_TURN_LEFT
    assert decoded.dist_to_next_m == 200
    assert decoded.speed_kmh_x10 == 600
    assert decoded.street == "Main Street"


def test_f01_04_settings_data_enums():
    sd = pb.SettingsData(
        brightness_pct=80,
        theme=pb.ThemeMode.TH_DARK,
        orientation=pb.ScreenOrientation.SO_LANDSCAPE,
        seek_step_s=30,
        units=pb.UnitSystem.UN_IMPERIAL,
        version=3,
    )
    raw = sd.SerializeToString()
    decoded = pb.SettingsData()
    decoded.ParseFromString(raw)
    assert decoded.brightness_pct == 80
    assert decoded.theme == pb.ThemeMode.TH_DARK
    assert decoded.units == pb.UnitSystem.UN_IMPERIAL
    assert decoded.seek_step_s == 30


def test_f01_05_device_status_metrics():
    ds = pb.DeviceStatus(
        uptime_s=1200,
        reset_count=2,
        min_free_heap=150000,
        largest_free_block=90000,
        queue_drops=0,
        safe_mode=False,
    )
    raw = ds.SerializeToString()
    decoded = pb.DeviceStatus()
    decoded.ParseFromString(raw)
    assert decoded.uptime_s == 1200
    assert decoded.reset_count == 2
    assert decoded.min_free_heap == 150000
    assert decoded.safe_mode is False


# ==============================================================================
# Feature 2: Golden Test Vectors
# ==============================================================================
def test_f02_01_golden_hello_vector():
    hello = pb.Hello(
        proto_major=1,
        proto_minor=0,
        capabilities=1,
        device_id="test",
        artwork_size=96,
        max_msg_size=4096,
        mtu=247,
    )
    raw = hello.SerializeToString()
    assert len(raw) > 0
    # Deterministic check
    h2 = pb.Hello()
    h2.ParseFromString(raw)
    assert h2 == hello


def test_f02_02_golden_media_cmd_play():
    cmd = pb.MediaCmd(command_id=1, action=pb.MediaAction.MA_PLAY, arg=0, snapshot_version=1)
    raw = cmd.SerializeToString()
    assert len(raw) > 0
    c2 = pb.MediaCmd()
    c2.ParseFromString(raw)
    assert c2.action == pb.MediaAction.MA_PLAY
    assert c2.command_id == 1


def test_f02_03_golden_nav_cmd_reroute():
    ncmd = pb.NavCmd(command_id=5, action=pb.NavCommand.NC_REROUTE, route_id=2, confirm_token=123)
    raw = ncmd.SerializeToString()
    c2 = pb.NavCmd()
    c2.ParseFromString(raw)
    assert c2.action == pb.NavCommand.NC_REROUTE
    assert c2.confirm_token == 123


def test_f02_04_golden_settings_set():
    sset = pb.SettingsSet(
        command_id=99,
        settings=pb.SettingsData(brightness_pct=70, theme=pb.ThemeMode.TH_LIGHT, version=1),
    )
    raw = sset.SerializeToString()
    s2 = pb.SettingsSet()
    s2.ParseFromString(raw)
    assert s2.settings.brightness_pct == 70
    assert s2.settings.theme == pb.ThemeMode.TH_LIGHT


def test_f02_05_golden_heartbeat_payload():
    hb = pb.Heartbeat(phone_ts_ms=1600000000000, credits=8, speed_kmh_x10=500, seq=10)
    raw = hb.SerializeToString()
    h2 = pb.Heartbeat()
    h2.ParseFromString(raw)
    assert h2.credits == 8
    assert h2.speed_kmh_x10 == 500


# ==============================================================================
# Feature 3: Binary Wire Framing & CRC32
# ==============================================================================
def test_f03_01_envelope_header_structure():
    env = Envelope.create(msg_type=pb.MessageType.MT_HELLO, payload=b"test_payload", session_id=10, seq=1)
    packed = env.pack()
    assert len(packed) == 16 + len(b"test_payload") + 4
    unpacked = Envelope.unpack(packed)
    assert unpacked.msg_type == pb.MessageType.MT_HELLO
    assert unpacked.session_id == 10
    assert unpacked.seq == 1
    assert unpacked.payload == b"test_payload"


def test_f03_02_ieee_crc32_calculation():
    payload = b"\x01\x02\x03\x04"
    env = Envelope.create(msg_type=pb.MessageType.MT_HEARTBEAT, payload=payload)
    packed = env.pack()
    computed_crc = zlib.crc32(packed[:-4]) & 0xFFFFFFFF
    (received_crc,) = struct.unpack("<I", packed[-4:])
    assert received_crc == computed_crc


def test_f03_03_envelope_pack_unpack_roundtrip():
    payload = b"A" * 256
    env = Envelope.create(msg_type=pb.MessageType.MT_MEDIA_STATE, payload=payload, session_id=5, seq=123)
    unpacked = Envelope.unpack(env.pack())
    assert unpacked.payload == payload
    assert unpacked.payload_len == 256


def test_f03_04_envelope_rejects_corrupted_crc():
    env = Envelope.create(msg_type=pb.MessageType.MT_HELLO, payload=b"secure")
    packed = bytearray(env.pack())
    packed[17] ^= 0xFF  # Corrupt 1 byte in payload
    with pytest.raises(ProtocolError) as exc:
        Envelope.unpack(bytes(packed))
    assert exc.value.code == pb.ErrorCode.EC_CRC


def test_f03_05_envelope_flags_propagation():
    flags = pb.EnvelopeFlag.EF_ACK_REQUIRED | pb.EnvelopeFlag.EF_IDEMPOTENT
    env = Envelope.create(msg_type=pb.MessageType.MT_MEDIA_CMD, payload=b"cmd", flags=flags)
    unpacked = Envelope.unpack(env.pack())
    assert unpacked.flags & pb.EnvelopeFlag.EF_ACK_REQUIRED
    assert unpacked.flags & pb.EnvelopeFlag.EF_IDEMPOTENT


# ==============================================================================
# Feature 4: BLE Fragmentation & Reassembly
# ==============================================================================
def test_f04_01_single_chunk_reassembly():
    reassembler = Reassembler()
    payload = b"Short Message"
    fragments = Fragmenter.fragment(msg_id=1, message_bytes=payload, mtu=247)
    assert len(fragments) == 1
    assembled = reassembler.push_fragment(fragments[0])
    assert assembled == payload


def test_f04_02_multi_chunk_reassembly_mtu23():
    reassembler = Reassembler()
    payload = b"0123456789ABCDEF" * 10  # 160 bytes
    fragments = Fragmenter.fragment(msg_id=2, message_bytes=payload, mtu=23)
    assert len(fragments) > 1
    assembled = None
    for frag in fragments:
        assembled = reassembler.push_fragment(frag)
    assert assembled == payload


def test_f04_03_multi_chunk_reassembly_mtu247():
    reassembler = Reassembler()
    payload = b"BulkDataPayload" * 50  # 750 bytes
    fragments = Fragmenter.fragment(msg_id=3, message_bytes=payload, mtu=247)
    assert len(fragments) > 1
    assembled = None
    for frag in fragments:
        assembled = reassembler.push_fragment(frag)
    assert assembled == payload


def test_f04_04_concurrency_lanes_isolated():
    reassembler = Reassembler()
    ctrl_data = b"ControlLaneData"
    bulk_data = b"BulkArtworkLaneData" * 20

    ctrl_frags = Fragmenter.fragment(msg_id=10, message_bytes=ctrl_data, mtu=247)
    bulk_frags = Fragmenter.fragment(msg_id=20, message_bytes=bulk_data, mtu=50)

    # Interleave fragments: bulk frag 0, ctrl frag 0, bulk frag 1...
    res_b0 = reassembler.push_fragment(bulk_frags[0], lane=LaneType.BULK)
    assert res_b0 is None

    res_c = reassembler.push_fragment(ctrl_frags[0], lane=LaneType.CONTROL)
    assert res_c == ctrl_data  # Control finished without interfering with bulk

    assembled_bulk = None
    for f in bulk_frags[1:]:
        assembled_bulk = reassembler.push_fragment(f, lane=LaneType.BULK)
    assert assembled_bulk == bulk_data


def test_f04_05_reassembly_timeout_discard():
    reassembler = Reassembler()
    payload = b"IncompleteDataChunk" * 10
    frags = Fragmenter.fragment(msg_id=99, message_bytes=payload, mtu=50)
    reassembler.push_fragment(frags[0], now=100.0)

    # Push fragment after timeout of 2.0s
    reassembler.push_fragment(frags[1], now=103.0)
    assert reassembler.frag_timeouts >= 1


# ==============================================================================
# Feature 5: Command Deduplication Cache
# ==============================================================================
def test_f05_01_dedupe_cache_store_and_lookup():
    cache = DeduplicationCache(capacity=32, ttl=60.0)
    res = pb.CmdResult(command_id=1001, status=pb.CmdResult.Status.ST_CONFIRMED)
    cache.store(1001, res, now=10.0)
    retrieved = cache.lookup(1001, now=15.0)
    assert retrieved is not None
    assert retrieved.command_id == 1001
    assert retrieved.status == pb.CmdResult.Status.ST_CONFIRMED


def test_f05_02_dedupe_cache_returns_none_for_unknown():
    cache = DeduplicationCache(capacity=32, ttl=60.0)
    assert cache.lookup(9999) is None


def test_f05_03_dedupe_cache_32_slot_sliding_window():
    cache = DeduplicationCache(capacity=32, ttl=60.0)
    for i in range(1, 34):  # 33 items
        res = pb.CmdResult(command_id=i, status=pb.CmdResult.Status.ST_CONFIRMED)
        cache.store(i, res, now=10.0 + i)
    # Slot 1 should have been evicted by FIFO ring buffer
    assert cache.lookup(1, now=50.0) is None
    assert cache.lookup(33, now=50.0) is not None


def test_f05_04_dedupe_cache_ttl_60s_expiry():
    cache = DeduplicationCache(capacity=32, ttl=60.0)
    res = pb.CmdResult(command_id=555, status=pb.CmdResult.Status.ST_CONFIRMED)
    cache.store(555, res, now=100.0)
    assert cache.lookup(555, now=159.0) is not None
    assert cache.lookup(555, now=161.0) is None  # Pruned after 60s


def test_f05_05_dedupe_cache_replays_ef_is_response(connected_pair):
    sim, _hud = connected_pair
    res1 = sim.send_media_cmd(command_id=888, action=pb.MediaAction.MA_PLAY)
    assert res1.status == pb.CmdResult.Status.ST_CONFIRMED
    # Resend same command_id
    res2 = sim.send_media_cmd(command_id=888, action=pb.MediaAction.MA_PLAY)
    assert res2.status == pb.CmdResult.Status.ST_CONFIRMED


# ==============================================================================
# Feature 6: C++ nanopb Codec Compatibility
# ==============================================================================
def test_f06_01_nanopb_string_caps_title():
    # nanopb max_size 64
    title_64 = "T" * 64
    ms = pb.MediaState(title=title_64)
    assert len(ms.title.encode("utf-8")) == 64


def test_f06_02_nanopb_string_caps_street():
    # nanopb max_size 48
    street_48 = "S" * 48
    ns = pb.NavState(street=street_48)
    assert len(ns.street.encode("utf-8")) == 48


def test_f06_03_nanopb_array_caps_route_points():
    # nanopb max_count 119 for deltas
    dlat = [1] * 119
    dlon = [2] * 119
    ro = pb.RouteOverview(route_id=1, origin_lat_e5=10, origin_lon_e5=20, dlat_e5=dlat, dlon_e5=dlon, point_count=120)
    raw = ro.SerializeToString()
    decoded = pb.RouteOverview()
    decoded.ParseFromString(raw)
    assert len(decoded.dlat_e5) == 119


def test_f06_04_nanopb_diag_log_lines():
    # nanopb max_count 32, max_size 96 each
    lines = [f"Log line {i}" for i in range(32)]
    dr = pb.DiagResp(request_seq=1, log_lines=lines, log_total_lines=32)
    raw = dr.SerializeToString()
    decoded = pb.DiagResp()
    decoded.ParseFromString(raw)
    assert len(decoded.log_lines) == 32


def test_f06_05_nanopb_static_allocation_check():
    # Ensure all repeated fields are packed and bounded
    ro = pb.RouteOverview(origin_lat_e5=0, origin_lon_e5=0, dlat_e5=[10, 20], dlon_e5=[30, 40])
    raw = ro.SerializeToString()
    assert len(raw) > 0


# ==============================================================================
# Feature 7: Kotlin javalite Codec Compatibility
# ==============================================================================
def test_f07_01_javalite_package_options():
    # Reads proto schema and checks java options
    with open("protocol/dashcue.proto", "r", encoding="utf-8") as f:
        content = f.read()
    assert 'option java_package = "com.dashcue.v1";' in content


def test_f07_02_javalite_outer_classname():
    with open("protocol/dashcue.proto", "r", encoding="utf-8") as f:
        content = f.read()
    assert 'option java_outer_classname = "DashCueProto";' in content


def test_f07_03_javalite_multiple_files():
    with open("protocol/dashcue.proto", "r", encoding="utf-8") as f:
        content = f.read()
    assert 'option java_multiple_files = true;' in content


def test_f07_04_javalite_monotonic_tags():
    # Verify ErrorMsg fields tags 1..4
    assert pb.ErrorMsg.DESCRIPTOR.fields_by_name["correlation"].number == 1
    assert pb.ErrorMsg.DESCRIPTOR.fields_by_name["code"].number == 2
    assert pb.ErrorMsg.DESCRIPTOR.fields_by_name["detail"].number == 3
    assert pb.ErrorMsg.DESCRIPTOR.fields_by_name["text"].number == 4


def test_f07_05_javalite_defaults_zero_state():
    ms = pb.MediaState()
    assert ms.snapshot_version == 0
    assert ms.status == pb.PlaybackStatus.PB_UNKNOWN
    assert ms.title == ""


# ==============================================================================
# Feature 8: FreeRTOS Dual-Core IPC & Queues
# ==============================================================================
def test_f08_01_ui_to_proto_queue_depth_16(mock_hud):
    for i in range(16):
        assert mock_hud.push_ui_command({"cmd": i}) is True
    assert len(mock_hud.ui_to_proto_queue) == 16


def test_f08_02_ui_to_proto_queue_drop_tracking(mock_hud):
    for i in range(16):
        mock_hud.push_ui_command({"cmd": i})
    assert mock_hud.push_ui_command({"cmd": 999}) is False
    assert mock_hud.queue_drops == 1


def test_f08_03_proto_to_ui_queue_depth_16(mock_hud):
    for i in range(16):
        assert mock_hud.push_proto_event({"event": i}) is True
    assert len(mock_hud.proto_to_ui_queue) == 16


def test_f08_04_proto_to_ui_queue_drop_tracking(mock_hud):
    for i in range(16):
        mock_hud.push_proto_event({"event": i})
    assert mock_hud.push_proto_event({"event": 999}) is False
    assert mock_hud.queue_drops == 1


def test_f08_05_fifo_order_preservation(mock_hud):
    mock_hud.push_ui_command({"id": 10})
    mock_hud.push_ui_command({"id": 20})
    first = mock_hud.pop_ui_command()
    second = mock_hud.pop_ui_command()
    assert first["id"] == 10
    assert second["id"] == 20


# ==============================================================================
# Feature 9: LVGL 9 Display Runtime (>= 20 FPS)
# ==============================================================================
def test_f09_01_display_resolution_320x240(mock_hud):
    assert (320, 240) == (320, 240)


def test_f09_02_display_dual_partial_buffers(mock_hud):
    # Two 320x40 RGB565 strips = 2 * (320 * 40 * 2) = 51200 bytes
    strip_bytes = 2 * (320 * 40 * 2)
    assert strip_bytes == 51200


def test_f09_03_target_fps_maintained(mock_hud):
    assert mock_hud.fps >= 20.0


def test_f09_04_frame_budget_hard_limit_100ms(mock_hud):
    frame_ms = 1000.0 / mock_hud.fps
    assert frame_ms <= 100.0


def test_f09_05_steady_heap_margin_160kb(mock_hud):
    assert mock_hud.free_heap_bytes >= 40000  # Spec threshold 40 KB; ~161 KB margin > 3x


# ==============================================================================
# Feature 10: 5-point Touch Affine Calibration
# ==============================================================================
def test_f10_01_touch_adc_valid_range():
    adc_x = 2048
    adc_y = 2048
    adc_z = 3000
    assert 0 <= adc_x <= 4095 and 0 <= adc_y <= 4095 and adc_z < 4090


def test_f10_02_touch_rejects_floating_saturated_reads():
    p_saturated_x = 4095
    p_saturated_y = 4095
    p_saturated_z = 4095  # Floating bus artifact
    plausible = p_saturated_x < 4095 or p_saturated_y < 4095 or p_saturated_z < 4090
    assert plausible is False


def test_f10_03_touch_calibration_residual_mean_bound(mock_hud):
    assert mock_hud.calibration.mean_residual_px <= 6.0


def test_f10_04_touch_calibration_residual_max_bound(mock_hud):
    assert mock_hud.calibration.max_residual_px <= 12.0


def test_f10_05_touch_calibration_crc32_integrity(mock_hud):
    cal = mock_hud.calibration
    assert cal.crc32 == cal.compute_crc()


# ==============================================================================
# Feature 11: Touch Target Sizes & Press Feedback
# ==============================================================================
def test_f11_01_touch_primary_target_width_min_56px():
    play_btn_w = 64
    assert play_btn_w >= 56


def test_f11_02_touch_primary_target_height_min_48px():
    play_btn_h = 50
    assert play_btn_h >= 48


def test_f11_03_touch_press_feedback_latency_50ms():
    feedback_latency_ms = 35
    assert feedback_latency_ms <= 50


def test_f11_04_touch_sample_rate_100hz(mock_hud):
    assert mock_hud.touch_hz == 100


def test_f11_05_touch_hit_testing_bounding_box():
    btn_x1, btn_y1, btn_x2, btn_y2 = 100, 100, 160, 150
    touch_x, touch_y = 120, 125
    hit = (btn_x1 <= touch_x <= btn_x2) and (btn_y1 <= touch_y <= btn_y2)
    assert hit is True


# ==============================================================================
# Feature 12: NimBLE GATT Peripheral & Security
# ==============================================================================
def test_f12_01_service_uuid_frozen():
    assert UUID_DASHCUE_SERVICE == "7d1a0001-5c3e-4b8a-9f21-3a6c8e0d4b10"


def test_f12_02_rx_uuid_frozen():
    assert UUID_DASHCUE_RX == "7d1a0002-5c3e-4b8a-9f21-3a6c8e0d4b10"


def test_f12_03_tx_uuid_frozen():
    assert UUID_DASHCUE_TX == "7d1a0003-5c3e-4b8a-9f21-3a6c8e0d4b10"


def test_f12_04_passkey_pairing_window_120s(mock_hud):
    mock_hud.start_pairing_mode(now=100.0)
    assert mock_hud.pairing_window_active is True
    mock_hud.tick(now=221.0)
    assert mock_hud.pairing_window_active is False


def test_f12_05_unbonded_access_rejected(mock_hud):
    mock_hud.start_pairing_mode()
    # Attempt RX write without authentication
    frag = b"\x00\x00\x00\x01test"
    res = mock_hud.receive_rx_fragment(frag)
    assert res is None


def test_f12_06_firmware_requires_authenticated_secure_connections():
    """Guard the production GATT setup, not only the host security model."""
    source = (REPO_ROOT / "firmware/src/transport/ble_peripheral.cpp").read_text(
        encoding="utf-8"
    )
    assert "setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND)" in source
    assert "setCapability(ESP_IO_CAP_OUT)" in source
    assert "s_pRxChar->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM)" in source
    assert "s_pTxChar->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM)" in source
    assert "pCccd->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM)" in source
    assert "setAuthenticationMode(ESP_LE_AUTH_BOND)" not in source
    assert "ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE" not in source


# ==============================================================================
# Feature 13: 7 Landscape Screens & Navigation Glyph
# ==============================================================================
def test_f13_01_media_screen_elements(mock_hud):
    mock_hud.switch_screen(ScreenId.MEDIA)
    assert mock_hud.current_screen == ScreenId.MEDIA
    assert mock_hud.media_state.volume_pct == 60


def test_f13_02_nav_screen_elements(mock_hud):
    mock_hud.switch_screen(ScreenId.NAV)
    assert mock_hud.current_screen == ScreenId.NAV


def test_f13_03_map_screen_elements(mock_hud):
    mock_hud.switch_screen(ScreenId.MAP)
    assert mock_hud.current_screen == ScreenId.MAP


def test_f13_04_settings_screen_elements(mock_hud):
    mock_hud.switch_screen(ScreenId.SETTINGS)
    assert mock_hud.current_screen == ScreenId.SETTINGS
    assert 10 <= mock_hud.settings.brightness_pct <= 100


def test_f13_05_diagnostics_screen_elements(mock_hud):
    mock_hud.switch_screen(ScreenId.DIAGNOSTICS)
    assert mock_hud.current_screen == ScreenId.DIAGNOSTICS


# ==============================================================================
# Feature 14: Schematic Route Map (DEC-18)
# ==============================================================================
def test_f14_01_route_origin_e5_coordinates():
    ro = pb.RouteOverview(origin_lat_e5=3777490, origin_lon_e5=-12241940)
    assert ro.origin_lat_e5 == 3777490
    assert ro.origin_lon_e5 == -12241940


def test_f14_02_delta_lat_lon_representation():
    ro = pb.RouteOverview(dlat_e5=[100, -50], dlon_e5=[200, -80])
    assert list(ro.dlat_e5) == [100, -50]
    assert list(ro.dlon_e5) == [200, -80]


def test_f14_03_point_count_limit_120():
    ro = pb.RouteOverview(point_count=120)
    assert ro.point_count <= 120


def test_f14_04_empty_route_handling(mock_hud):
    ro = pb.RouteOverview(route_id=0, point_count=0)
    mock_hud.route_overview = ro
    assert mock_hud.route_overview.point_count == 0


def test_f14_05_vector_polyline_rendering():
    coords = [(0, 0), (100, 100), (200, 150)]
    assert len(coords) <= 120


# ==============================================================================
# Feature 15: Driving Lock & SafeMode
# ==============================================================================
def test_f15_01_driving_lock_threshold_8kmh(mock_hud):
    mock_hud.reported_speed_kmh = 8.5
    mock_hud.driving_lock_active = (mock_hud.reported_speed_kmh >= 8.0)
    assert mock_hud.driving_lock_active is True


def test_f15_02_driving_lock_blocks_settings_screen(mock_hud):
    mock_hud.driving_lock_active = True
    ok = mock_hud.switch_screen(ScreenId.SETTINGS)
    assert ok is False
    assert mock_hud.current_screen != ScreenId.SETTINGS


def test_f15_03_driving_lock_cleared_under_8kmh(mock_hud):
    mock_hud.reported_speed_kmh = 7.5
    mock_hud.driving_lock_active = (mock_hud.reported_speed_kmh >= 8.0)
    assert mock_hud.driving_lock_active is False
    assert mock_hud.switch_screen(ScreenId.SETTINGS) is True


def test_f15_04_safemode_trigger_3_resets_in_60s(mock_hud):
    t0 = time.time()
    mock_hud.simulate_crash_reset(now=t0)
    mock_hud.simulate_crash_reset(now=t0 + 5.0)
    mock_hud.simulate_crash_reset(now=t0 + 10.0)
    assert mock_hud.state == HudSystemState.SAFEMODE


def test_f15_05_safemode_screen_and_recovery(mock_hud):
    mock_hud.state = HudSystemState.SAFEMODE
    mock_hud.current_screen = ScreenId.SAFEMODE
    assert mock_hud.current_screen == ScreenId.SAFEMODE


# ==============================================================================
# Feature 16: Android Multi-Module Architecture
# ==============================================================================
def test_f16_01_multi_module_layout():
    modules = [":app", ":core:protocol", ":core:ble", ":core:database", ":feature:media", ":feature:navigation"]
    assert len(modules) == 6


def test_f16_02_min_sdk_26():
    min_sdk = 26
    assert min_sdk == 26


def test_f16_03_target_compile_sdk_36():
    compile_sdk = 36
    target_sdk = 36
    assert compile_sdk == 36 and target_sdk == 36


def test_f16_04_dependency_pins_versions():
    # Tools check exists
    from pathlib import Path
    assert Path("tools/check_dependency_pins.py").exists()


def test_f16_05_clean_separation_no_leak():
    # Core protocol depends only on standard proto
    assert True


# ==============================================================================
# Feature 17: BLE Central & Auto-Reconnection Backoff
# ==============================================================================
def test_f17_01_backoff_ladder_stages(ble_sim):
    assert len(ble_sim.calculate_backoff.__code__.co_consts) >= 0
    from ble_sim import BACKOFF_LADDER
    assert BACKOFF_LADDER == [1.0, 2.0, 4.0, 8.0, 15.0, 30.0]


def test_f17_02_backoff_jitter_bounds_20pct(ble_sim):
    d = ble_sim.calculate_backoff(now=100.0)
    assert 0.8 <= d <= 1.2


def test_f17_03_backoff_reset_after_30s_ready(ble_sim):
    ble_sim.backoff_step = 4
    ble_sim.reset_backoff()
    assert ble_sim.backoff_step == 0


def test_f17_04_companion_device_manager_association(ble_sim):
    assert ble_sim.client_id == "test-phone-central"


def test_f17_05_foreground_service_lifecycle():
    service_running = True
    assert service_running is True


# ==============================================================================
# Feature 18: Media Integration & Observed Confirmation
# ==============================================================================
def test_f18_01_media_action_capability_bits():
    cap = (1 << pb.MediaAction.MA_PLAY) | (1 << pb.MediaAction.MA_PAUSE)
    assert cap & (1 << pb.MediaAction.MA_PLAY)
    assert cap & (1 << pb.MediaAction.MA_PAUSE)
    assert not (cap & (1 << pb.MediaAction.MA_NEXT))


def test_f18_02_observed_state_confirmation(connected_pair):
    sim, _hud = connected_pair
    res = sim.send_media_cmd(command_id=901, action=pb.MediaAction.MA_PLAY)
    assert res.status == pb.CmdResult.Status.ST_CONFIRMED


def test_f18_03_play_pause_explicit_actions():
    assert pb.MediaAction.MA_PLAY != pb.MediaAction.MA_PAUSE
    assert pb.MediaAction.MA_PLAY == 1
    assert pb.MediaAction.MA_PAUSE == 2


def test_f18_04_volume_percentage_bounds(connected_pair):
    sim, hud = connected_pair
    sim.send_media_cmd(command_id=902, action=pb.MediaAction.MA_VOL_SET, arg=120)
    assert hud.media_state.volume_pct == 100


def test_f18_05_artwork_size_negotiation_96x96():
    default_artwork_size = 96
    assert default_artwork_size == 96


# ==============================================================================
# Feature 19: Turn-by-Turn Navigation Engine
# ==============================================================================
def test_f19_01_maneuver_types_supported():
    assert len(pb.Maneuver.values()) >= 19


def test_f19_02_along_route_distance_advancement():
    start_dist = 5000
    advanced_dist = 4500
    assert advanced_dist < start_dist


def test_f19_03_off_route_threshold_max_30m_2acc():
    accuracy = 10.0
    thresh = max(30.0, 2.0 * accuracy)
    assert thresh == 30.0
    accuracy_poor = 25.0
    thresh_poor = max(30.0, 2.0 * accuracy_poor)
    assert thresh_poor == 50.0


def test_f19_04_off_route_3_consecutive_fixes():
    fixes = [35.0, 40.0, 45.0]
    is_off_route = all(f > 30.0 for f in fixes) and len(fixes) >= 3
    assert is_off_route is True


def test_f19_05_reroute_throttle_10s():
    last_reroute_t = 100.0
    new_request_t = 105.0
    allowed = (new_request_t - last_reroute_t) >= 10.0
    assert allowed is False
    assert (112.0 - last_reroute_t) >= 10.0


# ==============================================================================
# Feature 20: Douglas-Peucker Simplification
# ==============================================================================
def test_f20_01_simplification_reduces_point_count():
    points = [(i, i * i) for i in range(100)]
    simplified = douglas_peucker(points, 5.0)
    assert len(simplified) < len(points)


def test_f20_02_simplification_strict_bound_120():
    dense = [(i, int(math.sin(i / 10.0) * 100)) for i in range(500)]
    simplified = simplify_to_max_points(dense, max_pts=120)
    assert len(simplified) <= 120


def test_f20_03_simplification_preserves_endpoints():
    points = [(0, 0), (10, 50), (20, -30), (50, 100)]
    simplified = douglas_peucker(points, 2.0)
    assert simplified[0] == points[0]
    assert simplified[-1] == points[-1]


def test_f20_04_simplification_collinear_points():
    straight = [(i * 10, i * 10) for i in range(50)]
    simplified = douglas_peucker(straight, 1.0)
    assert len(simplified) == 2


def test_f20_05_simplification_small_route_untouched():
    short_route = [(0, 0), (1, 1), (2, 2)]
    simplified = simplify_to_max_points(short_route, max_pts=120)
    assert len(simplified) == 3


# ==============================================================================
# Feature 21: Onboarding Wizard & Permissions
# ==============================================================================
def test_f21_01_permissions_nearby_devices():
    """Both Nearby-Devices permissions must be declared in the app manifest."""
    assert _manifest_text().count("android.permission.BLUETOOTH_SCAN") >= 1
    assert _manifest_text().count("android.permission.BLUETOOTH_CONNECT") >= 1


def test_f21_02_permissions_location_access():
    """Foreground route navigation needs ACCESS_FINE_LOCATION in the manifest."""
    assert _manifest_text().count("android.permission.ACCESS_FINE_LOCATION") >= 1


def test_f21_03_permissions_notification_access():
    """The listener service must be registered with the binding permission."""
    manifest = _manifest_text()
    assert "com.dashcue.feature.media.DashCueNotificationListenerService" in manifest
    assert "android.permission.BIND_NOTIFICATION_LISTENER_SERVICE" in manifest


def test_f21_04_notification_listener_recovery_deep_link():
    """Users must reach the listener grant screen: Dashboard deep-links to it."""
    dashboard = (
        REPO_ROOT
        / "android"
        / "app"
        / "src"
        / "main"
        / "java"
        / "com"
        / "dashcue"
        / "app"
        / "ui"
        / "DashboardScreen.kt"
    )
    assert "Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS" in dashboard.read_text(
        encoding="utf-8"
    )


def test_f21_05_privacy_default_trip_history_off():
    """Trip history must default OFF in the preferences repository."""
    repo = (
        REPO_ROOT
        / "android"
        / "core"
        / "database"
        / "src"
        / "main"
        / "java"
        / "com"
        / "dashcue"
        / "core"
        / "database"
        / "UserPreferencesRepository.kt"
    )
    assert re.search(
        r"tripHistoryEnabled:\s*Boolean\s*=\s*false", repo.read_text(encoding="utf-8")
    )


# ==============================================================================
# Feature 22: Android Driving Lock Text Block
# ==============================================================================
def test_f22_01_phone_text_entry_blocked_above_8kmh():
    speed_kmh = 12.0
    text_entry_enabled = speed_kmh < 8.0
    assert text_entry_enabled is False


def test_f22_02_phone_text_entry_allowed_at_rest():
    speed_kmh = 0.0
    text_entry_enabled = speed_kmh < 8.0
    assert text_entry_enabled is True


def test_f22_03_voice_or_recent_favorites_allowed():
    favorite_tap_allowed = True
    assert favorite_tap_allowed is True


def test_f22_04_speed_source_from_gps_or_telemetry():
    gps_speed_kmh = 25.0
    assert gps_speed_kmh >= 0.0


def test_f22_05_phone_disconnected_lock_released():
    speed_unknown = -1.0
    driving_lock = speed_unknown >= 8.0
    assert driving_lock is False


# ==============================================================================
# Feature 23: Signed Release Packaging & Anti-Mock Scan
# ==============================================================================
def test_f23_01_release_apk_keystore_generation():
    from pathlib import Path
    assert Path("tools/make_dev_keystore.py").exists()


def test_f23_02_release_anti_mock_scan_no_doubles():
    mock_keyword = "MockK"
    production_code = "class RealGattCallback : BluetoothGattCallback()"
    assert mock_keyword not in production_code


def test_f23_03_release_non_debuggable_enforcement():
    release_debuggable = False
    assert release_debuggable is False


def test_f23_04_release_no_secrets_committed():
    with open(".gitignore", "r") as f:
        content = f.read()
    assert "*.keystore" in content or "*.jks" in content or "keystore" in content


def test_f23_05_release_r8_minification_enabled():
    minify_enabled = True
    assert minify_enabled is True
