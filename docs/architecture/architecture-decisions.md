# Architecture decisions — 2026-10-28 audit

## ADR-AUD-01: Preserve the existing component boundaries

**Context:** Android separates app, BLE, protocol and feature modules. Firmware separates transport/protocol, state/queues, UI and HALs. No verified defect required a broad rewrite.

**Decision:** Keep these boundaries and make narrow corrections where evidence showed a mismatch. **Alternatives:** replace the BLE stack, restructure modules, or rewrite state management. Rejected because they increase compatibility risk without measured benefit. **Impact:** smaller changes are easier to review; existing architectural limitations remain documented.

## ADR-AUD-02: Enforce authenticated BLE at GATT attributes

**Context:** source initially selected bond-only Just Works and open GATT permissions despite the documented authenticated/encrypted requirement. Passkey was also emitted in serial logs.

**Decision:** use pinned-stack Secure Connections + MITM + bonding, DisplayOnly passkey capability, encrypted/authenticated RX/TX/CCCD permissions; remove passkey from serial output. **Compatibility:** legacy bonds may require forget/re-pair. **Evidence required:** physical fresh-pair and unauthenticated-access rejection test; not yet done.

## ADR-AUD-03: Align Android payload cap with firmware

**Context:** firmware advertised 2,048-byte payloads but Android allowed 4,096 bytes; firmware reassembly storage is statically bounded.

**Decision:** cap Android encode/decode/reassembly at the firmware's 2,048-byte advertised contract. **Alternative:** enlarge firmware allocation; rejected without a memory budget and need. **Impact:** consistent peer behavior and lower Android bound; fixture bytes and tests updated.

## ADR-AUD-04: Bound and validate fragment credit input

**Context:** Android inbound fragment credit grants previously accumulated without a bound and malformed index/count values could reach later sequencing logic.

**Decision:** clamp the outbound credit pool to eight and accept credit grants only after validating nonzero fragment count and index range; require the encoded grant to fit a byte. **Impact:** malformed peers cannot inflate state indefinitely. This does not establish that the full credit-window algorithm is active end-to-end; current BLE sends are serialized with acknowledgements.

## ADR-AUD-05: Regenerate generated protocol/config outputs

**Context:** generated Nanopb and LVGL outputs drifted from their source schema/template.

**Decision:** use repository generators and check mode rather than manually editing generated headers. **Impact:** generated output diffs are expected and must be reviewed with source and fixture changes.

## Deferred

- No measured performance bottleneck justified optimization.
- No firmware payload ceiling increase, BLE stack migration, or broad UI/state rewrite.
- A2DP/I2S fault needs board/core diagnosis before choosing a product architecture change.
