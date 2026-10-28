# Data flow

## Connection and synchronization

1. Android `DashCueBleManager` scans for the fixed service UUID/name, connects, requests MTU 247, discovers the service, enables TX notifications through the CCCD, and sends HELLO.
2. It validates HELLO_ACK (major version, acceptance and peer payload cap), invokes snapshot resynchronization and transitions to READY only after synchronization.
3. Firmware `BleRxCallbacks` copies bounded ATT fragments to the BLE RX queue. The protocol task reassembles, checks the envelope and CRC, decodes protobuf, and dispatches to state/UI or a response.
4. Android writes are serialized and use write-with-response, awaiting each fragment callback. Actual negotiated MTU is used; a requested MTU is not assumed to have succeeded.

## User-facing flows present in source

| Data | Source → processing → transport/display | Verification boundary |
|---|---|---|
| Media | Android active MediaSession → media state / command dispatcher → `DashCueSyncBridge` → BLE `MEDIA_STATE` or `MEDIA_CMD` → firmware state/UI | Real player, command effect and physical display not observed in the current run |
| Navigation | OSRM route/location engine and recognized Maps/Waze navigation notification parser → bridge → `NAV_STATE` → firmware state/UI | Endpoint-dependent; current run had no connected app or route test |
| Settings | Android preferences or HUD settings → protobuf settings messages → bridge / firmware state | Source path exists; physical echo not tested in this audit |
| Artwork | Android media resolver has metadata artwork; bridge intentionally sends no artwork transfer. Firmware has `ARTWORK_BEGIN/CHUNK/END` consumer/store. | End-to-end transfer disabled, not a verified feature |
| Device status | Firmware telemetry / diagnostics → protocol notification → Android diagnostics state | No current phone/HUD session was established |
| Pairing | Explicit HUD pairing window → passkey displayed on HUD → Android association/OS pairing → authenticated GATT | Security configuration corrected; actual new authenticated pairing not retested |

## State ownership

Android GATT connection/readiness state is represented by `ReconnectionStateMachine`; firmware state is guarded through the application-state API and passed between protocol, UI and touch tasks via bounded IPC queues. Protocol framing is shared by two implementations, not one generated codec: both are checked against shared fixture bytes. External routing services and Android permissions are independent failure sources; their errors must not be mistaken for a BLE success.

## Data integrity

An envelope is `16-byte header + protobuf payload + 4-byte IEEE CRC32`. A fragment contains a 4-byte fragment header and at most `ATT_MTU - 7` frame bytes. Both endpoints enforce the 2,048-byte protobuf ceiling. These checks detect truncation/corruption; CRC is not cryptographic authentication. GATT security is separately responsible for link confidentiality/authentication.
