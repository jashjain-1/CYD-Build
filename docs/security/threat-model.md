# Threat model

## Assets and scope

Assets: control commands, media metadata, current navigation/destination, settings, and firmware availability/integrity. The product has a phone app, BLE peripheral, embedded tasks/display, and optional user-selected network routing endpoints. This is not a safety-certified vehicle system.

## Threat actors

- Nearby unauthenticated BLE central attempting discovery, connection, reads or command writes.
- Malformed or hostile peer sending invalid frames/fragments or exhausting bounded resources.
- Other Android apps supplying unexpected media metadata or notifications.
- Network endpoint returning malformed, stale or misleading routing data.
- Accidental loss of power, radio link, queue capacity, or corrupt persistent state.

## Controls present / changes in this audit

| Risk | Control in source | Status |
|---|---|---|
| Unauthenticated GATT access | **None — by product decision.** BLE pairing was removed: no bonding, no MITM, no passkey, RX/TX/CCCD are unauthenticated (see BUG-04 in [bug audit](../testing/bug-audit.md)) | Accepted risk. Any nearby central can read frames and write commands; only the protocol layer validates input |
| Passkey leaked via serial logs | No passkey exists any more (the GAP passkey handler was deleted) | Closed by removal |
| Frame corruption/hostile length | Frame, protobuf and fragment bounds; CRC; bounded lanes/queues | Unit/native and host checks; device fuzz/queue saturation not run |
| Stale connection state | Android GATT identity check and reconnect state machine; firmware clears session on disconnect | Code exists; reconnect cycles not run in this audit |
| Private destination/notification exposure | External endpoint is user-configured; parser only consumes app-relevant navigation notifications; no destination should be logged | Review source/logs before sharing; endpoint/network privacy not tested |
| Supply-chain/build secrets | Dependencies are pinned; local signing material excluded by project policy | Dependency audit currently needs rerun; no release APK scan result in this audit |

## Residual risk and required checks

CRC32 only detects accidental corruption. It is not message authentication, and with the BLE link deliberately unauthenticated it is the *only* integrity check on inbound frames. Treat every byte from the link as hostile: the bounded frame/fragment/dedupe paths and the protobuf field validation are the trust boundary, and they must be fuzzed (native `test_tc_pr_adv_*` cases cover framing, fragmentation, dedupe and malformed protobuf on the host).

Because the link is open, the meaningful compensating controls are physical and product-level: the HUD is inside the car, the reachable action set is media control, navigation display and settings, and no credential or personal content is served to a connected peer. Anyone relying on this HUD in a shared/public space should be told the link is open.

Review all Android permissions, exported components, network TLS policy and dependencies for the exact release artifact. Run malformed frame/fragment fuzzing, queue saturation and malformed external response tests. Keep signing keys, credentials, personal notification text and precise location out of Git and logs.

## Findings

SEC-01 (bond-only/Just Works and open GATT permissions) and SEC-02 (passkey printed in serial) are recorded in [audit findings](../audit/findings.md). SEC-01 is now closed by *removing authentication entirely* rather than strengthening it — an explicit product decision recorded in [bug audit](../testing/bug-audit.md) BUG-04 — so the open-GATT half of that finding is the intended design and the remaining mitigation is protocol-level validation. SEC-02 is closed because no passkey exists. No CVSS score or penetration-test pass is asserted.