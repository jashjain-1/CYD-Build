"""DashCue PC BLE Central Simulator and Opaque-Box Test Harness."""

from .ble_sim import BleCentralSim, CentralState
from .mock_hud import HudSystemState, MockHud, ScreenId, TouchCalibrationBlob
from .protocol import (
    ENVELOPE_HEADER_LEN,
    MAX_PAYLOAD_LEN,
    PROTO_MAJOR,
    PROTO_MINOR,
    TRAILER_LEN,
    UUID_DASHCUE_RX,
    UUID_DASHCUE_SERVICE,
    UUID_DASHCUE_TX,
    UUID_DIS_SERVICE,
    DeduplicationCache,
    Envelope,
    FlowController,
    Fragmenter,
    LaneType,
    ProtocolError,
    Reassembler,
)
from .scenario_runner import ScenarioRunner, simplify_to_max_points

__all__ = [
    "ENVELOPE_HEADER_LEN",
    "MAX_PAYLOAD_LEN",
    "PROTO_MAJOR",
    "PROTO_MINOR",
    "TRAILER_LEN",
    "UUID_DASHCUE_RX",
    "UUID_DASHCUE_SERVICE",
    "UUID_DASHCUE_TX",
    "UUID_DIS_SERVICE",
    "BleCentralSim",
    "CentralState",
    "DeduplicationCache",
    "Envelope",
    "FlowController",
    "Fragmenter",
    "HudSystemState",
    "LaneType",
    "MockHud",
    "ProtocolError",
    "Reassembler",
    "ScenarioRunner",
    "ScreenId",
    "TouchCalibrationBlob",
    "simplify_to_max_points",
]
