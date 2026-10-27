"""Shared Pytest Fixtures for DashCue HOST SIMULATION tests.

Provides MockHud instances, in-process BLE models, and test vectors. No radio access.
"""

import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
SIM_DIR = REPO_ROOT / "tools" / "test-only" / "ble_sim"

if str(SIM_DIR) not in sys.path:
    sys.path.insert(0, str(SIM_DIR))

from ble_sim import BleCentralSim, CentralState
from mock_hud import HudSystemState, MockHud


@pytest.fixture
def mock_hud():
    """Returns a freshly booted MockHud instance."""
    hud = MockHud()
    hud.start_pairing_mode()
    return hud


@pytest.fixture
def ble_sim(mock_hud):
    """Returns a BleCentralSim attached to mock_hud."""
    return BleCentralSim(hud=mock_hud, client_id="test-phone-central", mtu=247)


@pytest.fixture
def connected_pair(mock_hud, ble_sim):
    """Returns an authenticated, synchronized (sim, hud) pair in READY state."""
    ok = ble_sim.start_connection(passkey="123456")
    assert ok is True
    assert mock_hud.state == HudSystemState.READY
    assert ble_sim.state == CentralState.READY
    return ble_sim, mock_hud
