# Trust boundaries

| Boundary | Data crossing it | Controls present in source | Remaining verification |
|---|---|---|---|
| Android OS → app | Bluetooth, location, notification and media-session data | Runtime/manifest permissions and API guards; app uses active sessions and recognized navigation notification sources | Permission denial/revocation and OEM lifecycle tests are not part of this hardware run |
| Other Android apps → media/navigation adapters | Media metadata, playback state and notification fields | Parsers and protobuf size limits; errors are logged through app logger | Malformed/untrusted notification fuzzing and app compatibility matrix remain open |
| User-configured network endpoints → navigation | OSRM routes and Nominatim-compatible results, including destination data | Configurable endpoints; input/response handling lives in routing clients | No endpoint was configured; no network or privacy test run |
| Android ↔ BLE radio ↔ HUD | Control commands, telemetry, settings and media/navigation state | BLE GATT is intentionally unauthenticated (pairing removed); CRC, frame bounds and protocol parsing are not authentication | User confirms Maps/system operation on CYD; complete packet trace, unauthorized-peer test and reconnection matrix remain unrecorded |
| BLE callbacks → FreeRTOS tasks | ATT fragments | Length capped before copy; fixed-size packet, bounded queue, counted queue drops | Queue saturation and concurrent radio/display soak remain open |
| Protocol bytes → firmware state/UI | Protobuf payloads, image chunks and settings | CRC, frame/payload bounds, protobuf decoding and artwork checks | Fuzzing/hardware transfer and corrupted NVS scenarios need more coverage |
| Firmware state → physical display/audio | User information and signals | Display/touch/audio HALs and bounded queues | User confirms board produces sound; no captured CYD pixels, measured touch calibration, or controlled audio record in this session |

## Sensitive information

Do not log the pairing passkey, user destination, notification text, tokens or signing material. The BLE passkey serial disclosure found in this audit was removed. Device addresses and build identifiers in historical evidence are minimized where feasible; evidence should not be expanded with personal media/location data without review.

CRC32 provides accidental corruption detection only. It does not defend against a malicious peer. The BLE link is intentionally open after pairing removal; nearby peers may connect and send commands. The protocol parser is the input-validation boundary, not an identity/authentication boundary. Treat connected-device operation as appropriate only where this accepted product risk is understood. No penetration-test or regulatory-certification claim is made.
