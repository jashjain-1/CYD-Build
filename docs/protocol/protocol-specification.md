# DashCue protocol v1 — implemented wire contract

This document follows `protocol/dashcue.proto`, `protocol/uuids.md`, Android framing/fragmentation code, and firmware framing/fragmentation code. It describes the byte-level contract, not proof that every feature is end-to-end enabled. Message definitions and numeric IDs live in the schema.

## GATT

| Role | UUID | Direction/properties |
|---|---|---|
| Service | `7d1a0001-5c3e-4b8a-9f21-3a6c8e0d4b10` | Primary service |
| RX | `7d1a0002-5c3e-4b8a-9f21-3a6c8e0d4b10` | Phone → HUD, Write and Write Without Response |
| TX | `7d1a0003-5c3e-4b8a-9f21-3a6c8e0d4b10` | HUD → phone, Notify |

Firmware source configures LE Secure Connections, MITM and bond, authenticated/encrypted permissions for RX, TX and CCCD. This change has not yet been exercised by a fresh physical pairing.

## Frame envelope

All multi-byte values are little-endian. Fixed header fields are `magic:u16 (0xD45C)`, `version:u8 (1)`, `flags:u8`, `msg_type:u16`, `msg_id:u32`, `payload_length:u32`, `reserved:u16`; then protobuf payload and `crc32:u32`. Header size is 16 bytes, trailer 4 bytes. IEEE 802.3 CRC32 covers header plus payload. Reserved is currently zero. Payload maximum is 2,048 bytes, making maximum frame size 2,068 bytes:

\[
L_{frame}=16+L_{payload}+4,\qquad 0\le L_{payload}\le 2048.
\]

Receivers reject short/oversized frames, invalid magic/version, payload-length mismatch and CRC mismatch. CRC is not authentication.

## ATT fragmentation

Each notification/write ATT value begins with `lane:u8`, `total_fragments:u8`, `fragment_index:u8`, `credits:u8`, then frame bytes. Lane 0 is control, lane 1 bulk. Fragment indices start at 0 and must be ordered within a lane. Fragment chunk capacity is `ATT_MTU - 3-byte ATT header - 4-byte DashCue header`:

\[
L_{chunk}=ATT_{MTU}-7.
\]

MTU 23 is supported; MTU 247 is requested, not guaranteed. Fragment count is an unsigned byte and cannot exceed 255. Firmware has two bounded reassembly lanes and a 2,000 ms timeout. A complete frame is CRC-validated after reassembly. Firmware frame buffers and Android reassembly are bounded by the frame ceiling.

## Message families and direction

- Handshake/state: HELLO, HELLO_ACK, SNAPSHOT_REQUEST, HEARTBEAT, DEVICE_STATUS, DIAG_REQ/RESP.
- Media: MEDIA_STATE phone → HUD; MEDIA_CMD HUD → phone; CMD_RESULT and ERROR report outcomes.
- Artwork: ARTWORK_BEGIN/CHUNK/END phone → HUD in schema and firmware; Android bridge currently suppresses artwork sends, so feature is not connected end-to-end.
- Navigation: NAV_STATE phone → HUD, NAV_CMD HUD → phone, ROUTE_OVERVIEW phone → HUD.
- Settings: SETTINGS_SET and SETTINGS_STATE as defined by the schema and dispatch code.

Use the protobuf enum values, not prose or a second hand-maintained message table. Unknown fields follow protobuf compatibility; unsupported operations still require explicit result handling.

## Negotiation and implementation caveats

HELLO/ACK includes protocol version, capabilities, MTU and max payload. Android clamps outgoing payload to both local and peer-advertised maximum. Major mismatch is terminal. Firmware advertises 2,048 bytes. Android serializes acknowledged writes one fragment at a time; the fragment credit byte exists, but full cross-end credit-window behavior is not asserted as working. The implemented source includes message types that do not imply a connected user workflow.

See [test vectors](test-vectors.md), [UUID registry](../../protocol/uuids.md), and [audit findings](../audit/findings.md).