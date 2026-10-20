# DashCue BLE UUIDs (frozen)

Generated **once** and fixed. These values are part of protocol v1.0: changing
them breaks every paired HUD, so they are never regenerated (spec §9.1).

| Role | UUID | Properties |
|---|---|---|
| DashCue service | `7d1a0001-5c3e-4b8a-9f21-3a6c8e0d4b10` | primary service |
| RX (phone → HUD) | `7d1a0002-5c3e-4b8a-9f21-3a6c8e0d4b10` | Write, WriteWithoutResponse, encrypted + authenticated |
| TX (HUD → phone) | `7d1a0003-5c3e-4b8a-9f21-3a6c8e0d4b10` | Notify, encrypted + authenticated |
| Device Information service | `0000180a-0000-1000-8000-00805f9b34fb` | standard |
| Firmware Revision String | `00002a26-0000-1000-8000-00805f9b34fb` | standard, read |
| Hardware Revision String | `00002a27-0000-1000-8000-00805f9b34fb` | standard, read |
| Serial Number String | `00002a25-0000-1000-8000-00805f9b34fb` | standard, read, ESP32 MAC-derived id |

## Security requirements

- No characteristic is readable, writable **or** notifiable without a bond:
  GATT permissions require encryption + authentication (spec §9.7).
- The HUD advertises with the bonded phone on its accept-list and never scans
  or initiates (DEC-06).
- The HUD accepts exactly one central. A second connection attempt is refused;
  a bonded re-connect while the old link is still considered alive drops the
  old link (spec §9.6).

## Advertising payload (HUD → air)

| Field | Value |
|---|---|
| Flags | LE General Discoverable (only inside the 120 s pairing window), otherwise LE Limited/undirected non-discoverable with accept-list |
| Complete 128-bit Service UUID | `7d1a0001-5c3e-4b8a-9f21-3a6c8e0d4b10` |
| Shortened Local Name | `DashCue-HUD` (only while in pairing mode) |
| Service Data | firmware major/minor + board revision byte (lets the phone reject an obviously wrong HUD before connecting) |

A device name alone is never trusted for routing decisions; only the bonded
identity from the security layer is.

## 128-bit UUID generation note

These are hand-assigned, not registered. Only peripherals that a phone already
trusts are ever exposed, and no characteristic is reachable pre-bond, so the
practical risk of UUID collision with another vendor is limited to confusing a
scan-list UI, not to granting access.
