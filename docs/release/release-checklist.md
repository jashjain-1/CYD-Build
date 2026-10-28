# Release checklist — audit status 2026-10-28

This is a checklist, not a claim of release readiness. Every required item must have reproducible evidence or an explicit owner-approved waiver.

## Build, protocol and artifacts

- [ ] Run final `cd android && ./gradlew testDebugUnitTest lintDebug :app:assembleDebug :app:assembleRelease` and archive reports.
- [ ] Run `cd firmware && pio test -e native -v` and `pio run -e cyd_ili9341`; confirm exact hardware profile first.
- [ ] Run `python tools/gen_proto.py --check`, fixture comparison, `python tools/gen_lv_conf.py --check`, dependency pin/security review and release artifact scan.
- [ ] Verify APK signing/debuggable state and ensure local keystore, `keystore.properties`, local configuration and secrets are not staged.
- [ ] Review Git diff, evidence contents, licenses and dependency attribution.

## Device and security validation

- [ ] Resolve product board/panel profile vs physical hardware; never flash a mismatch.
- [ ] Diagnose I2S error; pass physical sound test if audio is advertised, or disable/label the feature honestly.
- [ ] Install APK; perform fresh Secure Connections + MITM pairing and verify unauthenticated access is denied.
- [ ] Exercise HELLO/snapshot, media command, settings roundtrip, navigation (with configured endpoint), disconnect/reconnect and real display/touch.
- [ ] Run 10 reconnect cycles, queue pressure and sustained soak; record results and raw logs with private data removed.
- [ ] Inspect UI on glass; verify permissions, backgrounding, process death and permission revocation on supported Android versions.

## Documentation and release decision

- [ ] Mermaid files syntax/render checked with a renderer; record tool/version or say not verified.
- [ ] Review `docs/testing/e2e-results.md`, `docs/testing/hardware-validation.md`, `docs/security/threat-model.md`, performance reports and unresolved findings.
- [ ] Ensure README claims match current implementation and status.
- [ ] Record a release owner decision, version and exact source commit.

**Current status: NOT READY.** The captured physical firmware reports an I2S registration failure, the app is absent from the handset, secure pairing/E2E and visible panel validation were not run, and the physical hardware profile must be confirmed against the image before flashing. See [findings](../audit/findings.md) and [hardware record](../testing/hardware-validation.md).