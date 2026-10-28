# Trust boundaries

| Boundary | Data crossing it | Controls present in source | Remaining verification |
|---|---|---|---|
| Android OS → app | Bluetooth, location, notification and media-session data | Runtime/manifest permissions and API guards; app uses active sessions and recognized navigation notification sources | Permission denial/revocation and OEM lifecycle tests are not part of this hardware run |
| Other Android apps → media/navigation adapters | Media metadata, playback state and notification fields | Parsers and protobuf size limits; errors are logged through app logger | Malformed/untrusted notification fuzzing and app compatibility matrix remain open |
| User-configured network endpoints → navigation | OSRM routes and Nominatim-compatible results, including destination data | Configurable endpoints; input/response handling lives in routing clients | No endpoint was configured; no network or privacy test run |
| Android ↔ BLE radio ↔ HUD | Pairing, control commands, telemetry, settings and media/navigation state | Source config requires SC+MITM bonding and encrypted/authenticated GATT attributes; envelope CRC and payload bounds | Physical pairing/access-control and traffic capture retest not run; legacy bonds may require forgetting and pairing again |
| BLE callbacks → FreeRTOS tasks | ATT fragments | Length capped before copy; fixed-size packet, bounded queue, counted queue drops | Queue saturation and concurrent radio/display soak remain open |
| Protocol bytes → firmware state/UI | Protobuf payloads, image chunks and settings | CRC, frame/payload bounds, protobuf decoding and artwork checks | Fuzzing/hardware transfer and corrupted NVS scenarios need more coverage |
| Firmware state → physical display/audio | User information and signals | Display/touch/audio HALs and bounded queues | Physical panel/profile mismatch and I2S error in latest capture; no visual or sound pass |

## Sensitive information

Do not log the pairing passkey, user destination, notification text, tokens or signing material. The BLE passkey serial disclosure found in this audit was removed. Device addresses and build identifiers in historical evidence are minimized where feasible; evidence should not be expanded with personal media/location data without review.

CRC32 provides accidental corruption detection only. It does not defend against a malicious peer. The trust model therefore depends on authenticated encrypted bonding and the single-peer policy, which must be confirmed on the actual target after a fresh bond. No penetration-test or regulatory-certification claim is made.
