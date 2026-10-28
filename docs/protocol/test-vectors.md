# Protocol test vectors

## Canonical fixtures

`protocol/fixtures/` contains 22 checked-in JSON descriptions, serialized protobuf `.bin` files, and framed `.frame` bytes; `protocol/fixtures/fixtures_data.h` embeds vectors for the firmware native tests. Fixtures exercise handshake, snapshots, media, artwork, navigation, settings, telemetry, commands and diagnostics. `VEC-02`, `VEC-03`, and `VEC-04` carry the negotiated 2,048-byte maximum in HELLO/HELLO_ACK.

The host simulator has its own generated schema copy for test-only behavior; it is not the production Android or firmware transport. Shared fixture tests and the platform unit/native tests are the authority for codec agreement.

## Regenerate and verify

From repository root, with PlatformIO's pinned Nanopb generator installed:

```bash
python protocol/generate_fixtures.py
python tools/gen_proto.py --check
```

Firmware native test command:

```bash
cd firmware
pio test -e native -v
```

Android framing/fragmentation tests:

```bash
cd android
./gradlew :core:protocol:testDebugUnitTest
```

## Coverage and limits

The fixture suite contains valid encoded frames and fragmentation variants at MTU 23 and 247. Unit/native suites additionally cover framing bounds, malformed CRC/length, boundary payloads and fragment behavior. Fixture agreement demonstrates deterministic byte-level compatibility for the listed inputs; it does not prove BLE timing, pairing, transfer completion on a radio, or display rendering. Add a vector when changing any field numbering, enum, framing constant, checksum or fragment layout; never silently regenerate and accept unexplained binary diffs.
