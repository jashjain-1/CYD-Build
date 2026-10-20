# Protocol v1.0

The phone and the HUD speak a small binary protocol over BLE GATT. This document is
the human-readable companion to the machine-readable schema; where the two ever
disagree, `protocol/dashcue.proto` wins and this file is the bug.

| Artefact | Role |
|---|---|
| [protocol/dashcue.proto](../protocol/dashcue.proto) | the single source of truth for every payload |
| [protocol/dashcue.options](../protocol/dashcue.options) | nanopb caps for every string and array |
| [protocol/uuids.md](../protocol/uuids.md) | the frozen GATT UUIDs and security requirements |
| [protocol/fixtures/](../protocol/fixtures/) | golden byte vectors both languages must reproduce |

**Implementation status:** the schema is written, and the **nanopb codec is generated**
from it into `firmware/src/protocol/generated/` by `tools/gen_proto.py`, whose `--check`
mode fails when that output drifts. Verified on the host: all `.options` caps land in the
generated header as fixed-size arrays (`char title[64]`, `int32_t dlat_e5[119]`,
`char log_lines[32][96]`) and the generated structs contain **no pointer fields**, so the
decode path allocates nothing on the heap as DEC-12 requires.

What does not exist yet: the Android javalite sources (M4, Gradle generates them),
the envelope/framing/fragmentation implementation on either side (M2/M3), and every
golden fixture in `protocol/fixtures/` (M2).

---

## 1. Transport

BLE GATT with the HUD as the peripheral and the phone as the central (DEC-05/06).
Nothing is readable, writable or notifiable before bonding: the connection is LE
Secure Connections with MITM protection, and the HUD shows a 6-digit passkey
(DEC-07).

* Just-works-style access is impossible by construction: characteristic permissions
  require encryption **and** authentication.
* The HUD accepts exactly one central. A second connection attempt is refused; a
  bonded reconnect while the old link is still considered alive drops the old link.
* The HUD advertises with the bonded phone on its accept-list and never scans or
  initiates. Reconnection belongs to the phone (DEC-06), which is the whole point of
  that decision: two reconnect loops race and produce flapping links.

Link preferences (§9.1): request ATT MTU 247 immediately (effective payload =
MTU − 3); request a 15-30 ms connection interval, latency 0, 4 s supervision timeout;
use data-length extension when available. **A 23-byte MTU must still work**, it costs
speed, not correctness.

## 2. Fragmentation

One ATT payload carries:

```
byte 0..1  msg_id    u16 LE   sender-local, wraps
byte 2     frag_idx  u8       0-based
byte 3     frag_cnt  u8       1..255
byte 4..   chunk
```

Rules:

* A reassembled message is at most **2048 bytes** (`DASHCUE_MAX_PAYLOAD_SIZE`). The cap
  is advertised in `HELLO_ACK.max_msg_size`, so a peer clamps what it sends instead of
  discovering the limit by rejection. `frag_cnt × chunk` is bounds-checked **before**
  allocating; anything larger is rejected with `EC_TOO_LARGE`.
* The largest message either end actually sends is an `ArtworkChunk` (1,024-byte payload);
  `RouteOverview` at 119 points stays under 1 KB. The cap was cut from 4,096 on
  2026-10-20 because each reassembly lane reserves a full frame buffer in static RAM.
* At most **2 concurrent reassemblies per direction**, one control lane, one bulk lane.
* Reassembly timeout 2 s → discard, count `frag_timeouts`.
* Out-of-order or duplicate fragments discard the whole message and count it; recovery
  relies on message-level retransmission, never on partial reassembly.
* The sender interleaves bulk-lane fragments with control messages **at fragment
  granularity**, so a control command is never stuck behind artwork.
* Write-Without-Response is used with an application-level credit scheme: the receiver
  grants credits (initial 8 fragments), the sender never exceeds outstanding credits,
  and the control lane keeps a reserved credit.

## 3. Envelope

After reassembly, little-endian:

```
u8   ver_major      v1 = 1
u8   ver_minor
u8   msg_type
u8   flags          bit0 ack_required, bit1 is_response, bit2 idempotent,
                    bit3 optional (a receiver may ignore this unknown type)
u16  session_id     random per connection; a mismatch drops the message
u32  seq            monotonic per sender per session
u32  correlation    command_id for commands, request seq for responses, else 0
u16  payload_len
u8[] payload        protobuf
u32  crc32          IEEE CRC32 over every preceding byte
```

Validation order is fixed, cheapest check first: length bounds → CRC → version →
session → type → payload decode. Every failure is a *counted* drop, and where the
correlation is known the receiver answers with `ERROR` carrying a code. A silently
dropped message is a bug, not a recovery strategy (Rule 9).

## 4. Message types

| Type | Id | Direction | Notes |
|---|---|---|---|
| HELLO / HELLO_ACK | 1 / 2 | both | versions, capabilities, device id, fw/hw revision, artwork size, max message size, MTU |
| SNAPSHOT_REQUEST | 3 | both | ask for full state |
| MEDIA_STATE | 10 | phone → HUD | full state, idempotent |
| MEDIA_CMD | 11 | HUD → phone | PLAY, PAUSE, NEXT, PREV, SEEK_REL, SEEK_ABS, VOL_STEP, VOL_SET, MUTE_SET |
| ARTWORK_BEGIN / CHUNK / END | 12 / 13 / 14 | phone → HUD | bulk lane, 96×96 RGB565 by default, per-chunk index, final CRC32, cancellable |
| NAV_STATE | 20 | phone → HUD | 1 Hz plus immediately on manoeuvre or status change |
| NAV_CMD | 21 | HUD → phone | END_NAVIGATION (needs `confirm_token`), REROUTE, MUTE_GUIDANCE (reserved) |
| ROUTE_OVERVIEW | 22 | phone → HUD | simplified polyline, bulk lane, versioned by `route_id` |
| SETTINGS_SET / SETTINGS_STATE | 30 / 31 | both | brightness, theme, orientation, units, seek step |
| DEVICE_STATUS | 40 | HUD → phone | heap, uptime, resets, queue drops, link stats |
| DIAG_REQ / DIAG_RESP | 41 / 42 | both | counters and a log tail on demand |
| HEARTBEAT | 50 | both | every 2 s: UTC ms, tz offset, credits, `speed_kmh_x10` or −1 |
| CMD_RESULT | 60 | both | the result of every command |
| ERROR | 61 | both | error code plus correlation |

`msg_type` values live in the `.proto` (`MessageType`) so both languages dispatch from
one table rather than from two hand-maintained switches.

## 5. Command semantics

This is the part the specification cares about most, because it is where "looks like
it worked" diverges from "worked".

* Every command carries a unique `command_id`. Receivers keep a **dedupe cache of the
  last 32 `command_id`s for 60 s** and replay the stored `CMD_RESULT` for a duplicate
  without executing it again.
* Retransmission: no `ACCEPTED` within **1 s** → resend the *same* `command_id`, at
  most twice. Because of the dedupe cache, NEXT/PREV/VOL_STEP can never double-apply.
  This is what makes rapid taps (TC-E2E-10) safe.
* Four result levels: `ACCEPTED` (the phone invoked the platform API), `CONFIRMED`
  (the effect was *observed*), `FAILED` with a reason, `UNCONFIRMED` (accepted, but no
  observed effect before the timeout).
* Confirm timeouts: PLAY/PAUSE 1.5 s, NEXT/PREV 3 s, SEEK 1.5 s, VOL 1 s.
* A command whose `snapshot_version` no longer matches the current session is rejected
  as `EC_STALE` rather than applied to the wrong track.
* Play and Pause are **explicit actions**, never a toggle (DEC-13). A toggle is
  unobservable from a glanceable UI and unsafe to retry.
* The HUD shows a pending state on press and updates **only** on observed
  `MEDIA_STATE`; a failure shows a short visible notice. A green indicator never means
  "packet sent" (Rule 4/5).

## 6. Version negotiation

`HELLO`/`HELLO_ACK` exchange major and minor versions.

* **Major must match exactly.** A mismatch is a terminal state on both sides with a
  visible explanation, never a best-effort decode.
* **Minor is the minimum of the two**, which is what makes additive change safe.
* Capability bits then decide which optional features are used (artwork, route
  overview, diagnostics, seek, touch calibration, the future Wi-Fi basemap).

## 7. Reconnection and synchronisation

**Phone:** `Disconnected → Scanning → Connecting → DiscoveringServices →
Negotiating → Synchronising → Ready`, plus `Backoff`, `ErrorBluetoothOff`,
`ErrorPermission`, `ErrorAuth`, `ErrorVersionMismatch`. Backoff 1, 2, 4, 8, 15, 30 s
(cap 30 s) with ±20 % jitter, reset after 30 s in `Ready`. Bluetooth-off and
permission-revoked are *terminal until fixed* states with an in-app prompt, not retry
loops.

**HUD:** `Booting → Advertising → (AdvertisingPairing) → ConnectedUnsynced → Ready →
Stale → Advertising`, plus `SafeMode`.

**Sync after connect:** HELLO/ACK → capability exchange → phone sends a full SNAPSHOT
(media, nav, settings, artwork) → HUD sends `DEVICE_STATUS` + `SETTINGS_STATE` →
`Ready`. After any reconnect the phone re-reads real state; **a previously sent
command is never assumed to have succeeded.**

**Heartbeat:** every 2 s; three misses (6 s) marks the link dead, the HUD enters
`Stale` and the phone starts reconnecting.

## 8. Errors

`ERROR` carries the offending message's correlation, a code from `ErrorCode` and a
short diagnostic string. The string is for logs and diagnostics screens, never shown
raw to a driver: driver-facing text is a HUD-owned, translated string, because a
protocol string is not a user interface.

Codes cover the cases that actually happen: `EC_TOO_LARGE`, `EC_CRC`,
`EC_VERSION_MISMATCH`, `EC_SESSION_MISMATCH`, `EC_UNKNOWN_TYPE`, `EC_DECODE`,
`EC_UNSUPPORTED`, `EC_BUSY`, `EC_NOT_BONDED`, `EC_INVALID_ARG`, `EC_STALE`,
`EC_NO_SESSION`, `EC_PERMISSION`, `EC_INTERNAL`, `EC_OFFLINE`, `EC_NO_ROUTE`.

## 9. Schema evolution

Non-negotiable, because both sides are flashed/installed independently:

* add fields, never renumber and never reuse a tag;
* never change a field's type or wire type;
* unknown fields are ignored; unknown enum values map to `*_UNKNOWN`/`*_NONE`;
* new enum **values** are appended, never inserted;
* every new `string`/`bytes`/`repeated` field needs a matching entry in
  `dashcue.options`, or nanopb fails the build by design.

The rule exists because the alternative, a firmware that decodes a newer phone's
message by luck, fails in the field, in a car, at night.
