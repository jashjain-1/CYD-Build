# Performance baseline — audit capture 2026-10-28

## Method

The latest physical serial capture contains boot and periodic firmware heap/render telemetry. It is one boot, one identified firmware session, not a controlled before/after benchmark. No Android timers, radio latency, throughput, repeated sample distribution, or A2DP playback measurements were collected.

## Raw observations

| Metric | Observation | Sample / condition | Status |
|---|---:|---|---|
| Boot to UI | 2,125 ms | One physical serial boot | Within the code's 3,000 ms boot target for this run |
| Free heap | 54,188 B | A reported periodic STATS sample | Observation only |
| Minimum free heap | 51,272 B | Same captured session | Observation only |
| Largest free block | 32,756 B | Same captured session | Observation only |
| `frame_max_ms` | 293 ms | Cumulative max telemetry | Exceeds 100 ms hard bound; boot/full paint may dominate, not isolated in this trace |
| `frame_avg_ms` | 4 ms at first sample, then 0 | Static UI / no pixel flush periods | Not a frame-rate benchmark; idle zeros indicate no rendered pixels |
| Stats span | Report says 15 s | Required 20 s for bounded stability | Stability NOT RUN |
| I2S install | Registration error | Same firmware boot | Audio measurement invalid; audio init failed |

Evidence: [serial capture](../evidence/20261028/hardware-audit-run/hud-serial.log) and [machine report](../evidence/20261028/hardware-audit-run/report.json).

## Baseline status

No comparable baseline-versus-final experiment exists for latency, BLE throughput, render FPS, reconnection, Android CPU/memory, or sustained heap drift. Do not calculate a percentage improvement from these observations. The logged `frame_max_ms=293` violates the stated 100 ms limit, although this capture does not separate boot-time from steady-state work. This should be measured with a repeatable animation and boot marker before changing the budget or claiming a performance regression/fix.
