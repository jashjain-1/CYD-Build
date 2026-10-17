"""DashCue PC BLE Central Simulator and Opaque-Box Test Harness."""

from .protocol import (
    PROTO_MAJOR,
    PROTO_MINOR,
    MAX_PAYLOAD_LEN,
    ENVELOPE_HEADER_LEN,
    TRAILER_LEN,
    UUID_DASHCUE_SERVICE,
    UUID_DASHCUE_RX,
    UUID_DASHCUE_TX,
    UUID_DIS_SERVICE,
    Envelope,
    Fragmenter,
    Reassembler,
    DeduplicationCache,
    FlowController,
    LaneType,
    ProtocolError,
)
from .mock_hud import MockHud, HudSystemState, ScreenId, TouchCalibrationBlob
from .ble_sim import BleCentralSim, CentralState
from .scenario_runner import ScenarioRunner, simplify_to_max_points

__all__ = [
    "PROTO_MAJOR",
    "PROTO_MINOR",
    "MAX_PAYLOAD_LEN",
    "ENVELOPE_HEADER_LEN",
    "TRAILER_LEN",
    "UUID_DASHCUE_SERVICE",
    "UUID_DASHCUE_RX",
    "UUID_DASHCUE_TX",
    "UUID_DIS_SERVICE",
    "Envelope",
    "Fragmenter",
    "Reassembler",
    "DeduplicationCache",
    "FlowController",
    "LaneType",
    "ProtocolError",
    "MockHud",
    "HudSystemState",
    "ScreenId",
    "TouchCalibrationBlob",
    "BleCentralSim",
    "CentralState",
    "ScenarioRunner",
    "simplify_to_max_points",
]
