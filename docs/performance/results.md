# Performance results

## 2026-10-28 audit

| Metric | Baseline | Final | Samples | Result |
|---|---:|---:|---:|---|
| Boot-to-UI | 2,125 ms in captured physical session | No separate final run | 1 | NOT COMPARABLE |
| Heap free / min / largest | 54,188 / 51,272 / 32,756 B | No separate final run | 1 capture | NOT COMPARABLE |
| Maximum frame telemetry | 293 ms cumulative | No separate final run | 1 capture | Observed value exceeds 100 ms hard limit; cause not isolated |
| BLE round-trip, MTU 23/247, reconnect p95 | NOT MEASURED | NOT MEASURED | 0 | NOT RUN |
| Artwork transfer / rendering | Disabled from Android bridge | NOT MEASURED | 0 | NOT APPLICABLE to active bridge path |
| A2DP playback/latency | I2S driver registration failed | NOT MEASURED | 0 valid playback samples | FAIL/BLOCKED |
| Android CPU/memory and UI latency | NOT MEASURED | NOT MEASURED | 0 | NOT RUN |

No speedup is claimed.

## Reproducible future measurements

- Run at least 10 cold boots on the exact target board/profile; record each boot-to-UI duration, median and p95.
- For BLE latency/throughput, record negotiated ATT MTU, peer/app/firmware version, transfer size, start/end monotonic timestamps, success/error count and radio conditions at MTU 23 and negotiated high MTU. Run at least 30 transfers per size.
- For frame time, trigger the same known animated screen for 60 seconds, keep boot excluded, capture per-frame samples and report median/p95/max and dropped frames. Distinguish idle flush-free time from rendered frames.
- For heap, log free/min/largest every 5 seconds over a 30-minute baseline and 8-hour soak; preserve reboot/reset and queue-drop evidence.
- For reconnect, perform 10 separately logged disconnect/reconnect cycles and report all durations and failures; do not report p95 with fewer than 20 samples.
- Fix and verify I2S initialization before measuring audio. Do not infer working sound from an audio task startup line.

Use the same board, app, firmware, endpoint, test payload and measurement definition before and after any optimization.