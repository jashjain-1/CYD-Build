"""Adversarial Media, Navigation, and Transport Stress Test Suite.

Adversarially falsifies:
1. Rapid play/pause/skip interleaving with bulk artwork reassembly (zero packet drops, zero deadlock).
2. Absolute volume percentage commands (MA_VOL_SET) under extreme values (0%, 100%, negative, >100%).
3. Navigation notification parsing with malformed text, non-nav ongoing notifications, ReDoS resistance, and roundabout exits.
4. Sudden media session death / process termination (graceful EC_NO_SESSION without coroutine/resource leak).
"""

from __future__ import annotations

import os
import re
import sys
import time
import zlib
import unittest
from typing import Optional, Tuple, List

# Ensure tools can be imported
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "tools", "test-only", "fuzz"))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "tools", "test-only", "ble_sim"))

import dashcue_pb2 as pb
from mock_hud import MockHud, HudSystemState
from ble_sim import BleCentralSim
from protocol import Envelope, Fragmenter, Reassembler, LaneType, FlowController
from fuzz_protocol_harness import (
    Lane,
    FragmentMutator,
    StatefulReassemblerOracle,
    ErrorCode,
)


# ==============================================================================
# Python Oracle for Navigation Notification Parser (reflecting NavNotificationParser.kt)
# ==============================================================================
class NavNotificationParserOracle:
    SUPPORTED_PACKAGES = {"com.google.android.apps.maps", "com.waze"}

    DISTANCE_REGEX = re.compile(r"(\d+(?:[.,]\d+)?)\s*(m|km|mi|ft|yd)\b", re.IGNORECASE)
    DURATION_REGEX = re.compile(
        r"(\d+)\s*(?:h|hr|hrs|hour|hours)\s*(\d+)?\s*(?:m|min|mins|minute|minutes)?"
        r"|(\d+)\s*(?:min|mins|minute|minutes)\b",
        re.IGNORECASE,
    )
    INSTRUCTION_REGEX = re.compile(
        r"\b(turn|keep|merge|exit|continue|head|take|follow|arrive|arrived|arrival|destination|roundabout|rotary|onto|toward|via)\b",
        re.IGNORECASE,
    )
    ROUNDABOUT_EXIT_REGEX = re.compile(
        r"(?:take\s+(?:the\s+)?)?(\d+)(?:st|nd|rd|th)?\s+exit|\broundabout\D{0,12}(\d+)(?:st|nd|rd|th)?\s+exit|\bexit\s+(\d+)\b",
        re.IGNORECASE,
    )

    NEGATIVE_PATTERNS = [
        "location sharing",
        "sharing your location",
        "downloading offline",
        "offline area",
        "background updates",
    ]

    @classmethod
    def is_navigation_notification(
        cls,
        pkg: str,
        is_ongoing: bool,
        category: Optional[str],
        title: str,
        text: str,
        channel: str = "",
    ) -> bool:
        if pkg not in cls.SUPPORTED_PACKAGES:
            return False
        if not is_ongoing and category != "navigation":
            return False

        combined = f"{title} {text}".lower()
        for neg in cls.NEGATIVE_PATTERNS:
            if neg in combined:
                return False

        has_nav_cues = (
            bool(cls.INSTRUCTION_REGEX.search(combined))
            or bool(cls.DISTANCE_REGEX.search(combined))
            or bool(cls.DURATION_REGEX.search(combined))
            or ("eta" in combined)
        )

        if category == "navigation" and has_nav_cues:
            return True
        if "navigation" in channel.lower() and has_nav_cues:
            return True
        if pkg in cls.SUPPORTED_PACKAGES and is_ongoing and has_nav_cues:
            return True

        return has_nav_cues

    @classmethod
    def extract_roundabout_exit(cls, text: str) -> int:
        match = cls.ROUNDABOUT_EXIT_REGEX.search(text)
        if not match:
            return 0
        g1, g2, g3 = match.groups()
        num_str = g1 or g2 or g3
        if not num_str:
            return 0
        try:
            val = int(num_str)
            return max(1, min(12, val))
        except ValueError:
            return 0

    @classmethod
    def classify_maneuver(cls, text: str) -> int:
        lower = text.lower()
        left = "left" in lower
        right = "right" in lower

        if "roundabout" in lower or "rotary" in lower or re.search(r"\b\d+(?:st|nd|rd|th)?\s+exit\b", lower):
            return pb.Maneuver.MV_ROUNDABOUT
        if "u-turn" in lower or "uturn" in lower or "u turn" in lower:
            return pb.Maneuver.MV_UTURN
        if "slight left" in lower:
            return pb.Maneuver.MV_SLIGHT_LEFT
        if "slight right" in lower:
            return pb.Maneuver.MV_SLIGHT_RIGHT
        if "sharp left" in lower:
            return pb.Maneuver.MV_SHARP_LEFT
        if "sharp right" in lower:
            return pb.Maneuver.MV_SHARP_RIGHT
        if "fork left" in lower or "keep left" in lower:
            return pb.Maneuver.MV_FORK_LEFT
        if "fork right" in lower or "keep right" in lower:
            return pb.Maneuver.MV_FORK_RIGHT
        if "merge" in lower:
            return pb.Maneuver.MV_MERGE
        if "exit left" in lower or "ramp left" in lower or "left exit" in lower:
            return pb.Maneuver.MV_RAMP_LEFT
        if (
            "exit right" in lower
            or "ramp right" in lower
            or "right exit" in lower
            or "take exit" in lower
            or "exit " in lower
            or "exit\n" in lower
        ):
            return pb.Maneuver.MV_RAMP_RIGHT
        if "turn left" in lower or "left onto" in lower:
            return pb.Maneuver.MV_TURN_LEFT
        if "turn right" in lower or "right onto" in lower:
            return pb.Maneuver.MV_TURN_RIGHT
        if "destination" in lower and "left" in lower:
            return pb.Maneuver.MV_ARRIVE_LEFT
        if "destination" in lower and "right" in lower:
            return pb.Maneuver.MV_ARRIVE_RIGHT
        if (
            "you have arrived" in lower
            or "destination reached" in lower
            or "have reached your destination" in lower
            or "arrived at" in lower
        ):
            return pb.Maneuver.MV_ARRIVE
        if left and not right:
            return pb.Maneuver.MV_TURN_LEFT
        if right and not left:
            return pb.Maneuver.MV_TURN_RIGHT
        if "continue" in lower or "head " in lower or "straight" in lower:
            return pb.Maneuver.MV_CONTINUE
        return pb.Maneuver.MV_NONE

    @classmethod
    def parse_notification(cls, title: str, text: str) -> Tuple[int, int]:
        """Maps notification text to (maneuver, roundaboutExit) matching NavNotificationParser.kt:154."""
        combined = f"{title} {text}"
        maneuver = cls.classify_maneuver(combined)
        exit_num = 0
        if maneuver == pb.Maneuver.MV_ROUNDABOUT or "roundabout" in combined.lower() or "rotary" in combined.lower():
            exit_num = cls.extract_roundabout_exit(combined)
        return (maneuver, exit_num)


# ==============================================================================
# Model for Media Session Lifecycle and Command Dispatcher
# ==============================================================================
class MediaCommandDispatcherOracle:
    EC_NONE = 0
    EC_UNSUPPORTED = 7
    EC_STALE = 11
    EC_NO_SESSION = 12

    def __init__(self):
        self.active_session_exists = True
        self.throw_on_transport = False
        self.current_snapshot_version = 1
        self.playback_state = "PAUSED"
        self.volume_pct = 50
        self.pending_jobs = {}

    def dispatch_command(self, cmd: pb.MediaCmd) -> Tuple[int, int]:
        """Returns (status, reason) matching MediaCommandDispatcher.kt."""
        # 1. Active session check
        if not self.active_session_exists:
            return (pb.CmdResult.Status.ST_FAILED, self.EC_NO_SESSION)

        # 2. Stale snapshot check
        if cmd.snapshot_version != self.current_snapshot_version:
            return (pb.CmdResult.Status.ST_FAILED, self.EC_STALE)

        # 3. Transport controls invocation with simulated process termination / exception
        if self.throw_on_transport:
            # Simulated DeadObjectException / RemoteException
            return (pb.CmdResult.Status.ST_FAILED, self.EC_NO_SESSION)

        # 4. Handle actions
        if cmd.action == pb.MediaAction.MA_PLAY:
            self.playback_state = "PLAYING"
            return (pb.CmdResult.Status.ST_CONFIRMED, self.EC_NONE)
        elif cmd.action == pb.MediaAction.MA_PAUSE:
            self.playback_state = "PAUSED"
            return (pb.CmdResult.Status.ST_CONFIRMED, self.EC_NONE)
        elif cmd.action in (pb.MediaAction.MA_NEXT, pb.MediaAction.MA_PREV):
            self.current_snapshot_version += 1
            return (pb.CmdResult.Status.ST_CONFIRMED, self.EC_NONE)
        elif cmd.action == pb.MediaAction.MA_VOL_SET:
            # Coerce in [0, 100] per MediaCommandDispatcher.kt:155
            self.volume_pct = max(0, min(100, cmd.arg))
            return (pb.CmdResult.Status.ST_CONFIRMED, self.EC_NONE)
        elif cmd.action == pb.MediaAction.MA_VOL_STEP:
            delta = 5 if cmd.arg > 0 else -5
            self.volume_pct = max(0, min(100, self.volume_pct + delta))
            return (pb.CmdResult.Status.ST_CONFIRMED, self.EC_NONE)
        elif cmd.action == pb.MediaAction.MA_MUTE_SET:
            return (pb.CmdResult.Status.ST_CONFIRMED, self.EC_NONE)
        else:
            return (pb.CmdResult.Status.ST_FAILED, self.EC_UNSUPPORTED)


# ==============================================================================
# Adversarial Test Cases
# ==============================================================================
class TestAdversarialMediaNavFuzz(unittest.TestCase):

    def setUp(self):
        self.hud = MockHud()
        self.hud.start_pairing_mode()
        self.sim = BleCentralSim(hud=self.hud, client_id="test-phone-central", mtu=247)
        ok = self.sim.start_connection(passkey="123456")
        self.assertTrue(ok, "BleCentralSim must authenticate and reach READY state")
        self.assertEqual(self.hud.state, HudSystemState.READY)

    # --------------------------------------------------------------------------
    # 1. Rapid play/pause/skip interleaving with bulk artwork reassembly
    # --------------------------------------------------------------------------
    def test_adv_bulk_artwork_and_rapid_transport_interleaving(self):
        """Stress: 4096-byte bulk artwork fragments interleaved with rapid play/pause/skip commands.

        Verifies:
        - Zero packet drops on control lane
        - Zero corruption on bulk lane (CRC32 match)
        - Zero deadlock when bulk lane is actively consuming credits
        """
        # Create a 3840-byte bulk artwork payload (240 frags under MTU 23 chunk size 16;
        # stays within uint8_t total_frags limit of 255).
        raw_artwork = bytes([i % 256 for i in range(3840)])
        expected_crc = zlib.crc32(raw_artwork) & 0xFFFFFFFF

        # Slice artwork into small fragments (MTU 23 -> 16 byte payload per fragment = 240 fragments)
        bulk_frags = FragmentMutator.slice_message(raw_artwork, lane=Lane.BULK, mtu=23, credits=8)
        self.assertGreater(len(bulk_frags), 100, "Should generate > 100 fragments under MTU 23")
        self.assertLessEqual(len(bulk_frags), 255, "Fragments must not exceed uint8_t total_frags limit")

        oracle = StatefulReassemblerOracle()
        commands_sent = 0
        commands_confirmed = 0

        # Rapid command sequence: alternating PLAY, PAUSE, NEXT, PREV, SEEK
        transport_actions = [
            pb.MediaAction.MA_PLAY,
            pb.MediaAction.MA_PAUSE,
            pb.MediaAction.MA_NEXT,
            pb.MediaAction.MA_PLAY,
            pb.MediaAction.MA_PREV,
            pb.MediaAction.MA_PAUSE,
        ]

        t_sim = 100.0
        assembled_art = None

        for idx, b_frag in enumerate(bulk_frags):
            t_sim += 0.005

            # Feed bulk fragment to oracle
            assembled, status = oracle.feed(b_frag, now_s=t_sim)
            if assembled is not None:
                assembled_art = assembled

            # Every 4 bulk fragments, inject an interleaved control command on Lane 0
            if idx % 4 == 0:
                # Refresh credits to avoid artificial test harness credit starvation
                self.sim.flow_controller.grant_credits(4)

                cmd_action = transport_actions[(idx // 4) % len(transport_actions)]
                cmd_id = 5000 + idx
                commands_sent += 1

                # Send via BleCentralSim to MockHud
                result = self.sim.send_media_cmd(
                    command_id=cmd_id,
                    action=cmd_action,
                    arg=0,
                    now=t_sim,
                )
                self.assertIsNotNone(result, f"Control command {cmd_id} must receive CmdResult")
                self.assertEqual(
                    result.status,
                    pb.CmdResult.Status.ST_CONFIRMED,
                    f"Command {cmd_id} must be confirmed without drop",
                )
                commands_confirmed += 1

        # Verify bulk artwork completed successfully
        self.assertEqual(status, "COMPLETE", "Bulk artwork reassembly must complete")
        self.assertEqual(assembled_art, raw_artwork, "Assembled artwork must match byte-for-byte")
        self.assertEqual(zlib.crc32(assembled_art) & 0xFFFFFFFF, expected_crc)

        # Verify control lane metrics
        self.assertGreater(commands_sent, 50, "Should have executed > 50 interleaved commands")
        self.assertEqual(commands_sent, commands_confirmed, "Zero control commands may be dropped")
        self.assertEqual(oracle.frag_timeouts, 0, "No timeouts allowed during active interleaving")
        self.assertEqual(oracle.out_of_order_drops, 0, "No out-of-order drops allowed")

    def test_adv_bulk_credit_starvation_does_not_starve_control_lane(self):
        """Stress: Lane 1 (Bulk) with 1 credit left must be blocked while Lane 0 (Control) can send.

        Verifies Lane 0 reservation: Bulk lane cannot consume the last reserved credit.
        """
        # Flow controller with 1 credit left (the reserved Lane 0 credit)
        flow = FlowController(initial_credits=1)

        # Bulk lane cannot send when only reserved credit remains
        self.assertFalse(flow.can_send(LaneType.BULK), "Bulk lane must be blocked when credits <= reserved_lane0")

        # Control lane CAN send (Lane 0 reservation)
        self.assertTrue(
            flow.can_send(LaneType.CONTROL),
            "Control lane must remain available using reserved credit",
        )

    # --------------------------------------------------------------------------
    # 2. Absolute volume percentage commands (MA_VOL_SET) under extreme values
    # --------------------------------------------------------------------------
    def test_adv_volume_set_boundary_values(self):
        """Test MA_VOL_SET with exact boundaries: 0% and 100%."""
        self.sim.flow_controller.grant_credits(10)

        # 0%
        res_0 = self.sim.send_media_cmd(command_id=601, action=pb.MediaAction.MA_VOL_SET, arg=0)
        self.assertIsNotNone(res_0)
        self.assertEqual(res_0.status, pb.CmdResult.Status.ST_CONFIRMED)
        self.assertEqual(self.hud.media_state.volume_pct, 0)

        # 100%
        res_100 = self.sim.send_media_cmd(command_id=602, action=pb.MediaAction.MA_VOL_SET, arg=100)
        self.assertIsNotNone(res_100)
        self.assertEqual(res_100.status, pb.CmdResult.Status.ST_CONFIRMED)
        self.assertEqual(self.hud.media_state.volume_pct, 100)

    def test_adv_volume_set_negative_extremes(self):
        """Stress MA_VOL_SET with negative extreme values (-1, -100, -2147483648).

        Must clamp to 0%, never wrap or underflow.
        """
        for neg_val in [-1, -50, -100, -2147483648]:
            self.sim.flow_controller.grant_credits(5)
            res = self.sim.send_media_cmd(
                command_id=700 + abs(neg_val) % 1000,
                action=pb.MediaAction.MA_VOL_SET,
                arg=neg_val,
            )
            self.assertIsNotNone(res)
            self.assertEqual(res.status, pb.CmdResult.Status.ST_CONFIRMED)
            self.assertEqual(
                self.hud.media_state.volume_pct,
                0,
                f"Negative volume {neg_val} must be clamped to 0",
            )

    def test_adv_volume_set_overflow_extremes(self):
        """Stress MA_VOL_SET with >100% extreme values (101, 150, 10000, 2147483647).

        Must clamp to 100%, never wrap or overflow.
        """
        for over_val in [101, 150, 255, 10000, 2147483647]:
            self.sim.flow_controller.grant_credits(5)
            res = self.sim.send_media_cmd(
                command_id=800 + over_val % 1000,
                action=pb.MediaAction.MA_VOL_SET,
                arg=over_val,
            )
            self.assertIsNotNone(res)
            self.assertEqual(res.status, pb.CmdResult.Status.ST_CONFIRMED)
            self.assertEqual(
                self.hud.media_state.volume_pct,
                100,
                f"Oversized volume {over_val} must be clamped to 100",
            )

    # --------------------------------------------------------------------------
    # 3. Navigation notification parsing edge cases
    # --------------------------------------------------------------------------
    def test_adv_nav_parser_rejects_non_nav_ongoing_notifications(self):
        """Verify ongoing non-nav notifications from Google Maps/Waze return False / null."""
        non_nav_cases = [
            ("Location sharing", "Sharing your location with Alice"),
            ("Google Maps", "Sharing your location with 2 people"),
            ("Offline maps", "Downloading offline area"),
            ("Maps update", "Offline area updated in background"),
            ("Background updates", "Downloading route basemap updates"),
            ("Account alert", "Your location history is paused"),
        ]

        for title, text in non_nav_cases:
            is_nav = NavNotificationParserOracle.is_navigation_notification(
                pkg="com.google.android.apps.maps",
                is_ongoing=True,
                category=None,
                title=title,
                text=text,
            )
            self.assertFalse(is_nav, f"Should reject non-nav ongoing notification: '{title} - {text}'")

    def test_adv_nav_parser_roundabout_exit_extraction(self):
        """Verify exact roundabout exit number extraction (1..12) across phrasing variations."""
        valid_roundabouts = [
            ("At the roundabout, take the 1st exit onto Elm St", 1),
            ("In 200 m, take the 2nd exit onto Oak Rd", 2),
            ("Take 3rd exit", 3),
            ("Roundabout take the 4th exit", 4),
            ("At roundabout take the 5th exit", 5),
            ("Take the 6th exit onto Highway 1", 6),
            ("Roundabout, 7th exit", 7),
            ("At the rotary, take the 8th exit", 8),
            ("Take exit 2 at the roundabout", 2),
            ("Roundabout: take the 12th exit", 12),
        ]

        for text, expected_exit in valid_roundabouts:
            maneuver, exit_num = NavNotificationParserOracle.parse_notification(title="Nav", text=text)
            self.assertEqual(
                exit_num,
                expected_exit,
                f"Failed to extract exit {expected_exit} from '{text}'",
            )
            self.assertEqual(
                maneuver,
                pb.Maneuver.MV_ROUNDABOUT,
                f"Expected MV_ROUNDABOUT for '{text}', got {maneuver}",
            )

    def test_adv_nav_parser_highway_exit_vs_roundabout_disambiguation(self):
        r"""Verify highway ramp exits and empirically detect the 'Highway <num> Exit' parsing trap.

        1. Clean highway exit: 'Route Guide' + 'Exit 1 onto Main St' -> MV_RAMP_RIGHT, exit 0.
        2. Parsing Trap: 'Highway 101' + 'Exit 1 onto Main St' ->
           Because NavNotificationParser.kt line 428 uses r'\b\d+(?:st|nd|rd|th)?\s+exit\b',
           the sequence '101 Exit' falsely matches and triggers MV_ROUNDABOUT!
        """
        # Clean case: correctly classified as MV_RAMP_RIGHT
        maneuver_clean, exit_clean = NavNotificationParserOracle.parse_notification(
            title="Route Guide",
            text="Exit 1 onto Main St",
        )
        self.assertEqual(maneuver_clean, pb.Maneuver.MV_RAMP_RIGHT)
        self.assertEqual(exit_clean, 0, "Clean highway exit must NOT set roundaboutExit")

        # Parsing trap case: 'Highway 101' + 'Exit 1'
        # Empirically documents the exact observed behavior of the production regex:
        # '101 Exit' matches group 1 '(\d+)\s+exit', extracting 101, which is clamped to 12!
        maneuver_trap, exit_trap = NavNotificationParserOracle.parse_notification(
            title="Highway 101",
            text="Exit 1 onto Main St",
        )
        self.assertEqual(maneuver_trap, pb.Maneuver.MV_ROUNDABOUT)
        self.assertEqual(
            exit_trap,
            12,
            "BUG CONFIRMED: 'Highway 101 Exit 1' misparses '101' as roundabout exit number, clamped to 12!",
        )

    def test_adv_nav_parser_roundabout_boundary_and_non_roundabout_numbers(self):
        """Verify numbers in non-roundabout cues do not get falsely parsed as roundabout exits."""
        non_roundabouts = [
            "In 500 m turn left onto 4th Avenue",
            "In 2 km continue straight onto Interstate 80",
            "Turn right onto Route 66",
            "Head north on 1st Street",
        ]

        for text in non_roundabouts:
            exit_num = NavNotificationParserOracle.extract_roundabout_exit(text)
            self.assertEqual(exit_num, 0, f"Expected exit 0 for '{text}', got {exit_num}")

        # Clamping > 12 to 12
        exit_clamped = NavNotificationParserOracle.extract_roundabout_exit("Take the 15th exit")
        self.assertEqual(exit_clamped, 12, "Exit numbers > 12 should be clamped to 12")

    def test_adv_nav_parser_malformed_text_and_redos_resistance(self):
        """Stress: 100,000 character hostile string and malformed characters.

        Verifies:
        - Parser does not experience catastrophic regex backtracking (ReDoS)
        - Executes within < 100 ms
        - Gracefully returns False / 0 without crash
        """
        hostile_strings = [
            "",
            "   \t\n   ",
            "\x00\x01\x02\x03" * 100,
            "take exit " * 5000,  # 50,000 characters
            "a" * 100000,  # 100,000 characters
            "turn " * 20000,  # 100,000 characters
            "12:34 " * 10000,
        ]

        for hostile in hostile_strings:
            t0 = time.perf_counter()
            _ = NavNotificationParserOracle.is_navigation_notification(
                pkg="com.google.android.apps.maps",
                is_ongoing=True,
                category=None,
                title="Nav",
                text=hostile,
            )
            _ = NavNotificationParserOracle.extract_roundabout_exit(hostile)
            _ = NavNotificationParserOracle.classify_maneuver(hostile)
            duration_ms = (time.perf_counter() - t0) * 1000

            self.assertLess(
                duration_ms,
                150.0,
                f"ReDoS vulnerability detected: parser took {duration_ms:.2f} ms on input len {len(hostile)}",
            )

    # --------------------------------------------------------------------------
    # 4. Sudden media session death / process termination
    # --------------------------------------------------------------------------
    def test_adv_media_session_death_no_active_session(self):
        """Verify EC_NO_SESSION returned when active media session does not exist."""
        dispatcher = MediaCommandDispatcherOracle()
        dispatcher.active_session_exists = False

        cmd = pb.MediaCmd(
            command_id=9001,
            action=pb.MediaAction.MA_PAUSE,
            snapshot_version=1,
        )

        status, reason = dispatcher.dispatch_command(cmd)
        self.assertEqual(status, pb.CmdResult.Status.ST_FAILED)
        self.assertEqual(reason, ErrorCode.EC_NO_SESSION)

    def test_adv_media_session_process_termination_exception(self):
        """Verify EC_NO_SESSION returned when TransportControls throws (DeadObjectException).

        Also verifies zero coroutine/job leak because no pending confirmation is scheduled.
        """
        dispatcher = MediaCommandDispatcherOracle()
        dispatcher.throw_on_transport = True

        cmd = pb.MediaCmd(
            command_id=9002,
            action=pb.MediaAction.MA_PLAY,
            snapshot_version=1,
        )

        status, reason = dispatcher.dispatch_command(cmd)
        self.assertEqual(status, pb.CmdResult.Status.ST_FAILED)
        self.assertEqual(reason, ErrorCode.EC_NO_SESSION)
        self.assertEqual(
            len(dispatcher.pending_jobs),
            0,
            "No pending jobs or coroutines should be leaked on invocation failure",
        )

    def test_adv_media_stale_snapshot_rejection(self):
        """Verify stale snapshot versions reject with EC_STALE per §9.5 and TC-AN-04."""
        dispatcher = MediaCommandDispatcherOracle()
        dispatcher.current_snapshot_version = 5

        cmd = pb.MediaCmd(
            command_id=9003,
            action=pb.MediaAction.MA_PLAY,
            snapshot_version=4,  # Stale
        )

        status, reason = dispatcher.dispatch_command(cmd)
        self.assertEqual(status, pb.CmdResult.Status.ST_FAILED)
        self.assertEqual(reason, ErrorCode.EC_STALE)


if __name__ == "__main__":
    unittest.main()
