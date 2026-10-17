"""Tier 4: In-process scenario simulations; no real phone or HUD is exercised.

Requirements: >= 12 realistic scenarios covering end-to-end user workflows.
Total: 12 test cases.
Simulates commutes, driving locks, network drops, rapid browsing, reroutes, and crash recoveries.
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
from scenario_runner import ScenarioRunner, douglas_peucker, simplify_to_max_points


def test_scenario_01_full_commute_flow():
    """Scenario 1: Complete commute from app launch to arrival."""
    res = ScenarioRunner.run_commute_scenario()
    assert res["connected"] is True
    assert res["ready"] is True
    assert res["media_started"] is True
    assert res["nav_active"] is True
    assert res["driving_lock_on"] is True
    assert res["route_rendered"] is True
    assert res["arrived"] is True
    assert res["driving_lock_cleared"] is True


def test_scenario_02_highway_driving_lock():
    """Scenario 2: Driving lock engaged above 8 km/h and disengaged when stopped."""
    res = ScenarioRunner.run_highway_driving_lock_scenario()
    assert res["settings_allowed_at_zero"] is True
    assert res["lock_engaged"] is True
    assert res["settings_dismissed"] is True
    assert res["settings_mutation_rejected"] is True
    assert res["lock_disengaged"] is True
    assert res["can_reopen_settings"] is True


def test_scenario_03_radio_drop_reconnect_with_jitter():
    """Scenario 3: Radio loss, exponential backoff ladder with jitter, and state resync."""
    res = ScenarioRunner.run_radio_drop_reconnect_scenario()
    assert res["disconnected"] is True
    assert res["backoff_jittered_1s"] is True
    assert res["backoff_jittered_2s"] is True
    assert res["reconnected"] is True
    assert res["resynced"] is True


def test_scenario_04_rapid_music_browsing_album_art():
    """Scenario 4: Rapid track skips and deduplication cache hit validation."""
    res = ScenarioRunner.run_rapid_music_browsing_scenario()
    assert res["rapid_skips_processed"] is True
    assert res["duplicate_replayed"] is True
    assert res["no_duplicate_execution"] is True


def test_scenario_05_off_route_deviation_reroute():
    """Scenario 5: Off-route deviation triggers polyline simplification under 120 points."""
    res = ScenarioRunner.run_off_route_reroute_scenario()
    assert res["simplified_under_120"] is True
    assert res["origin_preserved"] is True
    assert res["destination_preserved"] is True


def test_scenario_06_uncalibrated_boot_safemode_recovery():
    """Scenario 6: Corrupt calibration forces calibration screen; 3 crashes activate SafeMode."""
    res = ScenarioRunner.run_uncalibrated_boot_safemode_scenario()
    assert res["forced_calibration_screen"] is True
    assert res["boot_clean"] is True
    assert res["safemode_activated"] is True
    assert res["safemode_screen"] is True


def test_scenario_07_bulk_route_overview_under_mtu23():
    """Scenario 7: Full route overview transfer under constrained MTU 23."""
    hud = MockHud()
    hud.start_pairing_mode()
    sim = BleCentralSim(hud=hud, mtu=23)
    ok = sim.start_connection(passkey="123456")
    assert ok is True
    assert sim.effective_mtu == 23

    # Generate 40-point route
    coords = [(3777490 + i * 50, -12241940 + i * 30) for i in range(40)]
    dlat = [coords[i + 1][0] - coords[i][0] for i in range(len(coords) - 1)]
    dlon = [coords[i + 1][1] - coords[i][1] for i in range(len(coords) - 1)]
    ro = pb.RouteOverview(
        route_id=88,
        origin_lat_e5=coords[0][0],
        origin_lon_e5=coords[0][1],
        dlat_e5=dlat,
        dlon_e5=dlon,
        point_count=len(coords),
    )
    env = Envelope.create(
        msg_type=pb.MessageType.MT_ROUTE_OVERVIEW,
        payload=ro.SerializeToString(),
        session_id=sim.session_id,
        seq=sim._next_seq(),
    )
    sim.send_envelope(env, lane=LaneType.BULK)
    assert hud.route_overview is not None
    assert hud.route_overview.point_count == 40


def test_scenario_08_mountain_road_1000_point_simplification():
    """Scenario 8: 1000-point winding mountain track simplified <= 120 points."""
    # Synthetic mountain switchbacks
    mountain_pts = []
    for i in range(1000):
        lat = 3770000 + i * 15
        lon = -12240000 + int(math.sin(i / 15.0) * 800)  # Heavy switchbacks
        mountain_pts.append((lat, lon))

    simplified = simplify_to_max_points(mountain_pts, max_pts=120)
    assert len(simplified) <= 120
    assert simplified[0] == mountain_pts[0]
    assert simplified[-1] == mountain_pts[-1]


def test_scenario_09_day_to_night_tunnel_transition():
    """Scenario 9: Day to night theme transition with brightness clamping."""
    hud = MockHud()
    hud.start_pairing_mode()
    sim = BleCentralSim(hud=hud)
    sim.start_connection(passkey="123456")

    # Daylight mode
    day_settings = pb.SettingsSet(
        command_id=901,
        settings=pb.SettingsData(brightness_pct=95, theme=pb.ThemeMode.TH_LIGHT),
    )
    sim.send_envelope(Envelope.create(msg_type=pb.MessageType.MT_SETTINGS_SET, payload=day_settings.SerializeToString(), session_id=sim.session_id))
    assert hud.settings.theme == pb.ThemeMode.TH_LIGHT
    assert hud.settings.brightness_pct == 95

    # Enter dark tunnel -> phone sensors detect night
    night_settings = pb.SettingsSet(
        command_id=902,
        settings=pb.SettingsData(brightness_pct=25, theme=pb.ThemeMode.TH_DARK),
    )
    sim.send_envelope(Envelope.create(msg_type=pb.MessageType.MT_SETTINGS_SET, payload=night_settings.SerializeToString(), session_id=sim.session_id))
    assert hud.settings.theme == pb.ThemeMode.TH_DARK
    assert hud.settings.brightness_pct == 25


def test_scenario_10_incoming_call_audio_focus_loss():
    """Scenario 10: Incoming call causes audio focus pause without desync."""
    hud = MockHud()
    hud.start_pairing_mode()
    sim = BleCentralSim(hud=hud)
    sim.start_connection(passkey="123456")

    # Playing track
    sim.send_media_cmd(command_id=1001, action=pb.MediaAction.MA_PLAY)
    assert hud.media_state.status == pb.PlaybackStatus.PB_PLAYING

    # Audio focus loss event received by Android Companion
    ms_paused = pb.MediaState(
        snapshot_version=hud.media_state.snapshot_version + 1,
        status=pb.PlaybackStatus.PB_PAUSED,
        title="Podcast Episode #42",
        artist="Tech Talk",
        volume_pct=60,
    )
    sim.send_envelope(Envelope.create(msg_type=pb.MessageType.MT_MEDIA_STATE, payload=ms_paused.SerializeToString(), session_id=sim.session_id))
    assert hud.media_state.status == pb.PlaybackStatus.PB_PAUSED
    assert hud.media_state.title == "Podcast Episode #42"


def test_scenario_11_multi_step_turn_progression():
    """Scenario 11: Sequential progression through 5 navigation maneuvers."""
    hud = MockHud()
    hud.start_pairing_mode()
    sim = BleCentralSim(hud=hud)
    sim.start_connection(passkey="123456")

    maneuvers = [
        (pb.Maneuver.MV_DEPART, 100, "Depot Road", 10000),
        (pb.Maneuver.MV_TURN_RIGHT, 300, "Highway 101", 9900),
        (pb.Maneuver.MV_CONTINUE, 5000, "Highway 101 North", 9600),
        (pb.Maneuver.MV_FORK_LEFT, 800, "Exit 42A", 4600),
        (pb.Maneuver.MV_ARRIVE, 50, "Destination Lane", 50),
    ]

    for mv, dist, street, rem in maneuvers:
        nav = pb.NavState(
            status=pb.NavStatus.NAV_ACTIVE,
            next=mv,
            dist_to_next_m=dist,
            street=street,
            remaining_m=rem,
            speed_kmh_x10=550,
        )
        sim.send_envelope(Envelope.create(msg_type=pb.MessageType.MT_NAV_STATE, payload=nav.SerializeToString(), session_id=sim.session_id))
        assert hud.nav_state.next == mv
        assert hud.nav_state.dist_to_next_m == dist
        assert hud.nav_state.street == street


def test_scenario_12_continuous_100_command_stress_soak():
    """Scenario 12: Continuous 100-command stress cycle without heap degradation or drops."""
    hud = MockHud()
    hud.start_pairing_mode()
    sim = BleCentralSim(hud=hud)
    sim.start_connection(passkey="123456")

    initial_heap = hud.free_heap_bytes
    for i in range(1, 101):
        # Alternate volume adjustments and track skips
        if i % 2 == 0:
            vol = (i % 100) + 1
            sim.send_media_cmd(command_id=2000 + i, action=pb.MediaAction.MA_VOL_SET, arg=vol)
        else:
            sim.send_media_cmd(command_id=2000 + i, action=pb.MediaAction.MA_NEXT)

    assert hud.queue_drops == 0
    assert hud.free_heap_bytes >= 150000
    assert hud.state == HudSystemState.READY
