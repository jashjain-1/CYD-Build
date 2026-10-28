# Threat model

## Assets and scope

Assets: bonded-device identity, control commands, media metadata, current navigation/destination, settings, passkey, and firmware availability/integrity. The product has a phone app, BLE peripheral, embedded tasks/display, and optional user-selected network routing endpoints. This is not a safety-certified vehicle system.

## Threat actors

- Nearby unauthenticated BLE central attempting discovery, connection, reads or command writes.
- Malformed or hostile peer sending invalid frames/fragments or exhausting bounded resources.
- Other Android apps supplying unexpected media metadata or notifications.
- Network endpoint returning malformed, stale or misleading routing data.
- Accidental loss of power, radio link, queue capacity, or corrupt persistent state.

## Controls present / changes in this audit

| Risk | Control in source | Status |
|---|---|---|
| Unauthenticated GATT access | LE Secure Connections + MITM + bonding; DisplayOnly passkey; encrypted/authenticated RX/TX/CCCD permissions | Source corrected; fresh physical pair and access-denial test not run |
| Passkey leaked via serial logs | Passkey no longer formatted into serial log | Source corrected; final regression scan required |
| Frame corruption/hostile length | Frame, protobuf and fragment bounds; CRC; bounded lanes/queues | Unit/native and host checks; device fuzz/queue saturation not run |
| Stale connection state | Android GATT identity check and reconnect state machine; firmware clears session on disconnect | Code exists; reconnect cycles not run in this audit |
| Private destination/notification exposure | External endpoint is user-configured; parser only consumes app-relevant navigation notifications; no destination should be logged | Review source/logs before sharing; endpoint/network privacy not tested |
| Supply-chain/build secrets | Dependencies are pinned; local signing material excluded by project policy | Dependency audit currently needs rerun; no release APK scan result in this audit |

## Residual risk and required checks

CRC32 only detects accidental corruption. It is not message authentication and cannot compensate for weak pairing. Validate the actual stack's MITM behavior using a new bond, verify that an unbonded client cannot access protected attributes, and test legacy-bond removal. Review all Android permissions, exported components, network TLS policy and dependencies for the exact release artifact. Run malformed frame/fragment fuzzing, queue saturation and malformed external response tests. Keep signing keys, credentials, passkeys, personal notification text and precise location out of Git and logs.

## Findings

SEC-01 (bond-only/Just Works and open GATT permissions) and SEC-02 (passkey printed in serial) are recorded in [audit findings](../audit/findings.md). Both source remediations need hardware retesting before security closure. No CVSS score or penetration-test pass is asserted.