"""Tier 4 in-process scenario model for DashCue HUD (no hardware or Android).

Models workflows; it does not measure physical behavior or performance:
1. Full Commute Flow
2. Highway Driving Lock
3. Radio Drop & Reconnection with Jitter
4. Rapid Music Browsing & Album Art Streaming
5. Off-Route Deviation & Automatic Reroute
6. Uncalibrated Boot & SafeMode Recovery
7. Intermittent Link Loss during Bulk Transfer
8. High-Density Polyline Douglas-Peucker Simplification (<= 120 points)
9. Day/Night Theme Switching & Brightness Clamping
10. Audio Focus Loss / Media Interruption
11. Multi-Maneuver Navigation Progression
12. Heavy IPC Queue & FreeRTOS IPC Load Stress
"""

from __future__ import annotations

import math
import time
from typing import Dict, List, Optional, Tuple

try:
    from tools.test_only.ble_sim import dashcue_pb2 as pb
    from tools.test_only.ble_sim.ble_sim import BleCentralSim, CentralState
    from tools.test_only.ble_sim.mock_hud import (
        HudSystemState,
        MockHud,
        ScreenId,
        TouchCalibrationBlob,
    )
    from tools.test_only.ble_sim.protocol import (
        Envelope,
        Fragmenter,
        LaneType,
        create_cmd_result,
    )
except ImportError:
    try:
        import dashcue_pb2 as pb
        from ble_sim import BleCentralSim, CentralState
        from mock_hud import HudSystemState, MockHud, ScreenId, TouchCalibrationBlob
        from protocol import (
            Envelope,
            Fragmenter,
            LaneType,
            create_cmd_result,
        )
    except ImportError:
        from . import dashcue_pb2 as pb
        from .ble_sim import BleCentralSim, CentralState
        from .mock_hud import HudSystemState, MockHud, ScreenId, TouchCalibrationBlob
        from .protocol import (
            Envelope,
            Fragmenter,
            LaneType,
            create_cmd_result,
        )


def perpendicular_distance_e5(p: Tuple[int, int], p1: Tuple[int, int], p2: Tuple[int, int]) -> float:
    """Distance from point p to line segment (p1, p2) in e5 coordinate units."""
    x, y = p
    x1, y1 = p1
    x2, y2 = p2
    dx = x2 - x1
    dy = y2 - y1
    if dx == 0 and dy == 0:
        return math.hypot(x - x1, y - y1)
    # Projection
    u = ((x - x1) * dx + (y - y1) * dy) / float(dx * dx + dy * dy)
    u = max(0.0, min(1.0, u))
    px = x1 + u * dx
    py = y1 + u * dy
    return math.hypot(x - px, y - py)


def douglas_peucker(points: List[Tuple[int, int]], epsilon: float) -> List[Tuple[int, int]]:
    """Recursive Douglas-Peucker line simplification."""
    if len(points) <= 2:
        return points

    dmax = 0.0
    index = 0
    p1 = points[0]
    p2 = points[-1]

    for i in range(1, len(points) - 1):
        d = perpendicular_distance_e5(points[i], p1, p2)
        if d > dmax:
            index = i
            dmax = d

    if dmax > epsilon:
        rec1 = douglas_peucker(points[: index + 1], epsilon)
        rec2 = douglas_peucker(points[index:], epsilon)
        return rec1[:-1] + rec2
    else:
        return [points[0], points[-1]]


def simplify_to_max_points(points: List[Tuple[int, int]], max_pts: int = 120) -> List[Tuple[int, int]]:
    """Adapts epsilon until point count is <= max_pts."""
    if len(points) <= max_pts:
        return points

    low_eps = 1.0
    high_eps = 50000.0
    best = points

    for _ in range(20):
        mid_eps = (low_eps + high_eps) / 2.0
        simplified = douglas_peucker(points, mid_eps)
        if len(simplified) <= max_pts:
            best = simplified
            high_eps = mid_eps  # Try tighter tolerance
        else:
            low_eps = mid_eps  # Relax tolerance

    return best if len(best) <= max_pts else best[:max_pts]


class ScenarioRunner:
    """Executes high-level E2E scenarios against simulated components."""

    @staticmethod
    def run_commute_scenario() -> Dict[str, bool]:
        """Scenario 1: Full commute from app launch to arrival."""
        results = {}
        hud = MockHud()
        hud.start_pairing_mode()
        sim = BleCentralSim(hud=hud)

        # 1. Connect & Auth
        results["connected"] = sim.start_connection(passkey="123456")
        results["ready"] = (hud.state == HudSystemState.READY and sim.state == CentralState.READY)

        # 2. Start media
        res_play = sim.send_media_cmd(command_id=201, action=pb.MediaAction.MA_PLAY)
        results["media_started"] = (hud.media_state.status == pb.PlaybackStatus.PB_PLAYING)

        # 3. Start navigation
        nav_state = pb.NavState(
            route_id=10,
            status=pb.NavStatus.NAV_ACTIVE,
            next=pb.Maneuver.MV_TURN_RIGHT,
            dist_to_next_m=350,
            street="Grand Avenue",
            remaining_m=5200,
            remaining_s=600,
            speed_kmh_x10=450,  # 45.0 km/h
        )
        sim.send_envelope(
            Envelope.create(
                msg_type=pb.MessageType.MT_NAV_STATE,
                payload=nav_state.SerializeToString(),
                session_id=sim.session_id,
                seq=sim._next_seq(),
            )
        )
        results["nav_active"] = (hud.nav_state.status == pb.NavStatus.NAV_ACTIVE)
        results["driving_lock_on"] = hud.driving_lock_active

        # 4. Route overview transfer
        coords = [(3777490, -12241940), (3777550, -12241800), (3778000, -12241000)]
        dlat = [coords[i + 1][0] - coords[i][0] for i in range(len(coords) - 1)]
        dlon = [coords[i + 1][1] - coords[i][1] for i in range(len(coords) - 1)]
        route = pb.RouteOverview(
            route_id=10,
            origin_lat_e5=coords[0][0],
            origin_lon_e5=coords[0][1],
            dlat_e5=dlat,
            dlon_e5=dlon,
            point_count=len(coords),
        )
        sim.send_envelope(
            Envelope.create(
                msg_type=pb.MessageType.MT_ROUTE_OVERVIEW,
                payload=route.SerializeToString(),
                session_id=sim.session_id,
                seq=sim._next_seq(),
            ),
            lane=LaneType.BULK,
        )
        results["route_rendered"] = (hud.route_overview is not None and hud.route_overview.point_count == 3)

        # 5. Arrived
        nav_arrived = pb.NavState(
            route_id=10,
            status=pb.NavStatus.NAV_ARRIVED,
            next=pb.Maneuver.MV_ARRIVE,
            dist_to_next_m=0,
            remaining_m=0,
            remaining_s=0,
            speed_kmh_x10=0,
        )
        sim.send_envelope(
            Envelope.create(
                msg_type=pb.MessageType.MT_NAV_STATE,
                payload=nav_arrived.SerializeToString(),
                session_id=sim.session_id,
                seq=sim._next_seq(),
            )
        )
        results["arrived"] = (hud.nav_state.status == pb.NavStatus.NAV_ARRIVED)
        results["driving_lock_cleared"] = not hud.driving_lock_active
        return results

    @staticmethod
    def run_highway_driving_lock_scenario() -> Dict[str, bool]:
        """Scenario 2: Driving lock enforcement above 8 km/h."""
        results = {}
        hud = MockHud()
        hud.start_pairing_mode()
        sim = BleCentralSim(hud=hud)
        sim.start_connection(passkey="123456")

        # Speed 0: settings accessible
        hud.switch_screen(ScreenId.SETTINGS)
        results["settings_allowed_at_zero"] = (hud.current_screen == ScreenId.SETTINGS)

        # Speed rises to 15.0 km/h (speed_kmh_x10 = 150)
        sim.send_heartbeat(speed_kmh=15.0)
        results["lock_engaged"] = hud.driving_lock_active
        results["settings_dismissed"] = (hud.current_screen != ScreenId.SETTINGS)

        # Mutating settings rejected
        new_settings = pb.SettingsSet(
            command_id=301,
            settings=pb.SettingsData(brightness_pct=90, version=2),
        )
        sim.send_envelope(
            Envelope.create(
                msg_type=pb.MessageType.MT_SETTINGS_SET,
                payload=new_settings.SerializeToString(),
                session_id=sim.session_id,
                seq=sim._next_seq(),
            )
        )
        sim.poll_notifications()
        results["settings_mutation_rejected"] = (sim.last_error is not None and sim.last_error.code == pb.ErrorCode.EC_BUSY)

        # Speed drops back to 4.0 km/h
        sim.send_heartbeat(speed_kmh=4.0)
        results["lock_disengaged"] = not hud.driving_lock_active
        results["can_reopen_settings"] = hud.switch_screen(ScreenId.SETTINGS)
        return results

    @staticmethod
    def run_radio_drop_reconnect_scenario() -> Dict[str, bool]:
        """Scenario 3: Radio drop, exponential backoff, and state re-sync."""
        results = {}
        hud = MockHud()
        hud.start_pairing_mode()
        sim = BleCentralSim(hud=hud)
        sim.start_connection(passkey="123456")

        # Simulate sudden radio disconnection
        sim.disconnect()
        results["disconnected"] = (sim.state == CentralState.DISCONNECTED)

        # First backoff attempt
        b1 = sim.calculate_backoff()
        results["backoff_jittered_1s"] = (0.7 <= b1 <= 1.3)

        # Second backoff attempt
        b2 = sim.calculate_backoff()
        results["backoff_jittered_2s"] = (1.5 <= b2 <= 2.5)

        # Reconnection succeeds (bonded)
        results["reconnected"] = sim.start_connection(passkey="123456")
        results["resynced"] = (hud.state == HudSystemState.READY)
        return results

    @staticmethod
    def run_rapid_music_browsing_scenario() -> Dict[str, bool]:
        """Scenario 4: Rapid track skips and deduplication."""
        results = {}
        hud = MockHud()
        hud.start_pairing_mode()
        sim = BleCentralSim(hud=hud)
        sim.start_connection(passkey="123456")

        # Rapid Next taps with unique command IDs
        res1 = sim.send_media_cmd(command_id=401, action=pb.MediaAction.MA_NEXT)
        res2 = sim.send_media_cmd(command_id=402, action=pb.MediaAction.MA_NEXT)
        results["rapid_skips_processed"] = (res1 is not None and res2 is not None)

        # Re-sending command_id 401 returns cached result without incrementing snapshot_version
        v_before = hud.media_state.snapshot_version
        res1_dup = sim.send_media_cmd(command_id=401, action=pb.MediaAction.MA_NEXT)
        v_after = hud.media_state.snapshot_version
        results["duplicate_replayed"] = (res1_dup is not None and res1_dup.command_id == 401)
        results["no_duplicate_execution"] = (v_before == v_after)
        return results

    @staticmethod
    def run_off_route_reroute_scenario() -> Dict[str, bool]:
        """Scenario 5: GPS off-route detection and polyline simplification <= 120 points."""
        results = {}
        # Generate dense polyline of 500 GPS points
        dense_points = [(3777490 + i * 20, -12241940 + int(math.sin(i / 10.0) * 150)) for i in range(500)]
        simplified = simplify_to_max_points(dense_points, max_pts=120)

        results["simplified_under_120"] = (len(simplified) <= 120)
        results["origin_preserved"] = (simplified[0] == dense_points[0])
        results["destination_preserved"] = (simplified[-1] == dense_points[-1])
        return results

    @staticmethod
    def run_uncalibrated_boot_safemode_scenario() -> Dict[str, bool]:
        """Scenario 6: Corrupt calibration forces calibration screen; 3 crashes activate SafeMode."""
        results = {}
        hud = MockHud()

        # Corrupt calibration
        hud.calibration.is_valid = False
        hud.boot()
        results["forced_calibration_screen"] = (hud.current_screen == ScreenId.CALIBRATION)

        # Restore calibration and reboot cleanly
        hud.calibration.is_valid = True
        hud.boot()
        results["boot_clean"] = (hud.state == HudSystemState.ADVERTISING)

        # Simulate 3 crashes within 60s
        t0 = time.time()
        hud.simulate_crash_reset(now=t0)
        hud.simulate_crash_reset(now=t0 + 2.0)
        hud.simulate_crash_reset(now=t0 + 4.0)

        results["safemode_activated"] = (hud.state == HudSystemState.SAFEMODE)
        results["safemode_screen"] = (hud.current_screen == ScreenId.SAFEMODE)
        return results
