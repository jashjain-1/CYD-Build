#!/usr/bin/env python3
"""Canonical Golden Test Vector Generator for DashCue Protocol.

Covers all message types in protocol/dashcue.proto:
Generates raw protobuf binaries (.bin), enveloped frames (.frame),
and complete JSON test vectors (.json) in protocol/fixtures/,
as well as a C/C++ header (fixtures_data.h) for native unit tests.
"""

from __future__ import annotations

import binascii
import json
import struct
import sys
from pathlib import Path

# Add ble_sim to path to import dashcue_pb2
REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "tools" / "test-only" / "ble_sim"))

import dashcue_pb2 as pb
from google.protobuf.json_format import MessageToDict

FIXTURES_DIR = REPO_ROOT / "protocol" / "fixtures"
FIXTURES_DIR.mkdir(parents=True, exist_ok=True)

FRAME_MAGIC = 0xD45C
FRAME_VERSION = 0x01


def calc_crc32(data: bytes) -> int:
    """Standard IEEE 802.3 CRC32 (polynomial 0xEDB88320)."""
    return binascii.crc32(data) & 0xFFFFFFFF


def create_frame(msg_type: int, msg_id: int, flags: int, payload: bytes) -> bytes:
    """Pack 16-byte header + payload + 4-byte CRC32."""
    header = struct.pack(
        "<HBBHIIH",
        FRAME_MAGIC,
        FRAME_VERSION,
        flags,
        msg_type,
        msg_id,
        len(payload),
        0,  # reserved
    )
    content = header + payload
    crc = calc_crc32(content)
    trailer = struct.pack("<I", crc)
    return content + trailer


def fragment_frame(frame: bytes, lane: int, mtu: int) -> list[bytes]:
    """Fragment frame for given ATT MTU (chunk capacity = MTU - 7)."""
    chunk_size = mtu - 7
    total_frags = (len(frame) + chunk_size - 1) // chunk_size
    if total_frags == 0:
        total_frags = 1
    frags = []
    for i in range(total_frags):
        chunk = frame[i * chunk_size : (i + 1) * chunk_size]
        hdr = struct.pack("<BBBB", lane, total_frags, i, 8)  # 8 credits
        frags.append(hdr + chunk)
    return frags


def main():
    print("Generating DashCue canonical golden test vectors...")

    vectors = [
        # 1. Hello minimal
        (
            "VEC-01-HELLO-MIN",
            pb.MT_HELLO,
            "MT_HELLO",
            "Minimal Hello message with default scalar values",
            pb.Hello(proto_major=1, proto_minor=0, capabilities=0),
            1001,
            0,
            0,  # Lane 0
        ),
        # 2. Hello full
        (
            "VEC-02-HELLO-FULL",
            pb.MT_HELLO,
            "MT_HELLO",
            "Full Hello message with full device metadata and caps",
            pb.Hello(
                proto_major=1,
                proto_minor=0,
                capabilities=(
                    pb.CAP_ARTWORK
                    | pb.CAP_ROUTE_OVERVIEW
                    | pb.CAP_DIAGNOSTICS
                    | pb.CAP_SEEK
                ),
                device_id="D4E9F46FF088",
                fw_version="1.0.0-rc1",
                hw_revision="CYD-2432S028R-v3",
                artwork_size=96,
                max_msg_size=4096,
                mtu=247,
                window_max=8,
                flags=0,
                phone_ts_ms=1729056000000,
            ),
            1002,
            0,
            0,
        ),
        # 3. HelloAck accept
        (
            "VEC-03-HELLO-ACK-ACCEPT",
            pb.MT_HELLO_ACK,
            "MT_HELLO_ACK",
            "HelloAck accepting connection with negotiated parameters",
            pb.HelloAck(
                proto_major=1,
                proto_minor=0,
                capabilities=pb.CAP_ARTWORK | pb.CAP_ROUTE_OVERVIEW,
                fw_version="1.0.0",
                hw_revision="v3",
                max_msg_size=4096,
                mtu=247,
                accepted=1,
                reject_reason=0,
            ),
            1003,
            pb.EF_IS_RESPONSE,
            0,
        ),
        # 4. HelloAck reject
        (
            "VEC-04-HELLO-ACK-REJECT",
            pb.MT_HELLO_ACK,
            "MT_HELLO_ACK",
            "HelloAck rejecting session due to protocol version mismatch",
            pb.HelloAck(
                proto_major=1,
                proto_minor=0,
                capabilities=0,
                fw_version="1.0.0",
                hw_revision="v3",
                max_msg_size=4096,
                mtu=247,
                accepted=0,
                reject_reason=pb.EC_VERSION_MISMATCH,
            ),
            1004,
            pb.EF_IS_RESPONSE,
            0,
        ),
        # 5. SnapshotRequest
        (
            "VEC-05-SNAPSHOT-REQ",
            pb.MT_SNAPSHOT_REQUEST,
            "MT_SNAPSHOT_REQUEST",
            "SnapshotRequest requesting full sync across all subsystems",
            pb.SnapshotRequest(request_seq=42, want_media=15),
            1005,
            pb.EF_ACK_REQUIRED,
            0,
        ),
        # 6. MediaState
        (
            "VEC-06-MEDIA-STATE",
            pb.MT_MEDIA_STATE,
            "MT_MEDIA_STATE",
            "Active media playback state with track metadata and capabilities",
            pb.MediaState(
                snapshot_version=42,
                status=pb.PB_PLAYING,
                title="Bohemian Rhapsody",
                artist="Queen",
                album="A Night at the Opera",
                app_name="Spotify",
                capabilities=15,
                position_ms=120500,
                position_updated_at_ms=300000,
                duration_ms=354000,
                playback_speed=1.0,
                volume_pct=80,
                muted=False,
                artwork_id=1001,
                phone_ts_ms=1729056000000,
            ),
            1006,
            pb.EF_IDEMPOTENT,
            0,
        ),
        # 7. MediaCmd play
        (
            "VEC-07-MEDIA-CMD-PLAY",
            pb.MT_MEDIA_CMD,
            "MT_MEDIA_CMD",
            "Media command: explicit PLAY action",
            pb.MediaCmd(
                command_id=5001,
                action=pb.MA_PLAY,
                arg=0,
                snapshot_version=42,
            ),
            1007,
            pb.EF_ACK_REQUIRED,
            0,
        ),
        # 8. MediaCmd seek relative
        (
            "VEC-08-MEDIA-CMD-SEEK",
            pb.MT_MEDIA_CMD,
            "MT_MEDIA_CMD",
            "Media command: relative seek back by 10 seconds",
            pb.MediaCmd(
                command_id=5002,
                action=pb.MA_SEEK_REL,
                arg=-10000,
                snapshot_version=42,
            ),
            1008,
            pb.EF_ACK_REQUIRED,
            0,
        ),
        # 9. ArtworkBegin
        (
            "VEC-09-ARTWORK-BEGIN",
            pb.MT_ARTWORK_BEGIN,
            "MT_ARTWORK_BEGIN",
            "Artwork transfer begin metadata: 96x96 RGB565 image",
            pb.ArtworkBegin(
                artwork_id=1001,
                width=96,
                height=96,
                format=0,
                total_bytes=18432,
                total_crc32=0x12345678,
            ),
            1009,
            0,
            1,  # Bulk lane
        ),
        # 10. ArtworkChunk
        (
            "VEC-10-ARTWORK-CHUNK",
            pb.MT_ARTWORK_CHUNK,
            "MT_ARTWORK_CHUNK",
            "Artwork image data chunk containing 1024 bytes",
            pb.ArtworkChunk(
                artwork_id=1001,
                chunk_index=0,
                offset=0,
                data=b"\x1F\x00" * 512,  # 1024 bytes
            ),
            1010,
            0,
            1,  # Bulk lane
        ),
        # 11. ArtworkEnd
        (
            "VEC-11-ARTWORK-END",
            pb.MT_ARTWORK_END,
            "MT_ARTWORK_END",
            "Artwork transfer completion with full image verification CRC",
            pb.ArtworkEnd(
                artwork_id=1001,
                received_crc32=0x12345678,
                ok=True,
            ),
            1011,
            0,
            1,  # Bulk lane
        ),
        # 12. NavState
        (
            "VEC-12-NAV-STATE-MANEUVER",
            pb.MT_NAV_STATE,
            "MT_NAV_STATE",
            "Navigation guidance: active roundabout maneuver and speed telemetry",
            pb.NavState(
                route_id=2001,
                status=pb.NAV_ACTIVE,
                next=pb.MV_ROUNDABOUT,
                dist_to_next_m=250,
                street="Rotary Way",
                roundabout_exit=2,
                left_hand_traffic=False,
                following=pb.MV_TURN_RIGHT,
                dist_to_following_m=1200,
                remaining_m=15400,
                remaining_s=920,
                route_segment=3,
                segment_offset_m=50,
                heading_deg=180.5,
                speed_kmh_x10=650,
                gps_accuracy_m=4.2,
                has_live_traffic=False,
                phone_ts_ms=1729056000000,
            ),
            1012,
            pb.EF_IDEMPOTENT,
            0,
        ),
        # 13. NavCmd
        (
            "VEC-13-NAV-CMD-END",
            pb.MT_NAV_CMD,
            "MT_NAV_CMD",
            "Navigation command: END_NAVIGATION with confirmation token",
            pb.NavCmd(
                command_id=5003,
                action=pb.NC_END_NAVIGATION,
                route_id=2001,
                confirm_token=8888,
            ),
            1013,
            pb.EF_ACK_REQUIRED,
            0,
        ),
        # 14. RouteOverview
        (
            "VEC-14-ROUTE-OVERVIEW",
            pb.MT_ROUTE_OVERVIEW,
            "MT_ROUTE_OVERVIEW",
            "Route polyline overview with 6 points in delta-e5 coordinates",
            pb.RouteOverview(
                route_id=2001,
                origin_lat_e5=3777490,
                origin_lon_e5=-12241940,
                dlat_e5=[10, 20, -5, 15, -20],
                dlon_e5=[-15, 25, 10, -5, 30],
                point_count=6,
            ),
            1014,
            0,
            1,  # Bulk lane
        ),
        # 15. SettingsSet
        (
            "VEC-15-SETTINGS-SET",
            pb.MT_SETTINGS_SET,
            "MT_SETTINGS_SET",
            "SettingsSet: set brightness, dark theme, and orientation",
            pb.SettingsSet(
                command_id=5004,
                settings=pb.SettingsData(
                    brightness_pct=85,
                    theme=pb.TH_DARK,
                    orientation=pb.SO_LANDSCAPE_180,
                    left_hand_traffic=False,
                    seek_step_s=10,
                    units=pb.UN_METRIC,
                    version=1,
                ),
            ),
            1015,
            pb.EF_ACK_REQUIRED,
            0,
        ),
        # 16. SettingsState
        (
            "VEC-16-SETTINGS-STATE",
            pb.MT_SETTINGS_STATE,
            "MT_SETTINGS_STATE",
            "SettingsState: device confirmed settings payload",
            pb.SettingsState(
                settings=pb.SettingsData(
                    brightness_pct=85,
                    theme=pb.TH_DARK,
                    orientation=pb.SO_LANDSCAPE_180,
                    left_hand_traffic=False,
                    seek_step_s=10,
                    units=pb.UN_METRIC,
                    version=1,
                ),
                confirmed=True,
            ),
            1016,
            pb.EF_IDEMPOTENT,
            0,
        ),
        # 17. DeviceStatus
        (
            "VEC-17-DEVICE-STATUS",
            pb.MT_DEVICE_STATUS,
            "MT_DEVICE_STATUS",
            "Device health, free heap, FPS, link stats and error counters",
            pb.DeviceStatus(
                uptime_s=3600,
                reset_count=1,
                min_free_heap=161280,
                largest_free_block=120000,
                queue_drops=0,
                crc_failures=0,
                frag_timeouts=0,
                duplicate_commands=0,
                rssi_dbm=-65,
                mtu=247,
                conn_interval_us=15000,
                ui_avg_ms=28,
                ui_p95_ms=35,
                ui_max_ms=42,
                touch_hz=100,
                app_free_bytes=1048576,
                safe_mode=False,
                reset_reason=1,
            ),
            1017,
            pb.EF_IDEMPOTENT,
            0,
        ),
        # 18. Heartbeat
        (
            "VEC-18-HEARTBEAT",
            pb.MT_HEARTBEAT,
            "MT_HEARTBEAT",
            "Periodic heartbeat: timestamps, credit grant, and driving speed",
            pb.Heartbeat(
                phone_ts_ms=1729056000000,
                tz_offset_min=-420,
                credits=8,
                speed_kmh_x10=650,
                seq=100,
                rtt_ms=25,
            ),
            1018,
            0,
            0,
        ),
        # 19. CmdResult
        (
            "VEC-19-CMD-RESULT-CONFIRMED",
            pb.MT_CMD_RESULT,
            "MT_CMD_RESULT",
            "Command execution result: confirmed effect with timestamp",
            pb.CmdResult(
                command_id=5001,
                status=pb.CmdResult.ST_CONFIRMED,
                reason=pb.EC_NONE,
                observed_at_ms=1729056000500,
            ),
            1019,
            pb.EF_IS_RESPONSE,
            0,
        ),
        # 20. ErrorMsg
        (
            "VEC-20-ERROR-MSG",
            pb.MT_ERROR,
            "MT_ERROR",
            "Protocol error report: CRC corruption with diagnostic detail",
            pb.ErrorMsg(
                correlation=5002,
                code=pb.EC_CRC,
                detail=0,
                text="CRC32 checksum mismatch in frame",
            ),
            1020,
            pb.EF_IS_RESPONSE,
            0,
        ),
        # 21. DiagReq
        (
            "VEC-21-DIAG-REQ",
            pb.MT_DIAG_REQ,
            "MT_DIAG_REQ",
            "Diagnostic request: query counters and 10 log tail lines",
            pb.DiagReq(
                command_id=5005,
                want=3,
                log_tail_lines=10,
            ),
            1021,
            pb.EF_ACK_REQUIRED,
            0,
        ),
        # 22. DiagResp
        (
            "VEC-22-DIAG-RESP",
            pb.MT_DIAG_RESP,
            "MT_DIAG_RESP",
            "Diagnostic response: device counters and log tail entries",
            pb.DiagResp(
                request_seq=1021,
                status=pb.DeviceStatus(
                    uptime_s=3600,
                    reset_count=1,
                    min_free_heap=161280,
                    largest_free_block=120000,
                    queue_drops=0,
                    crc_failures=0,
                    frag_timeouts=0,
                    duplicate_commands=0,
                    rssi_dbm=-65,
                    mtu=247,
                    conn_interval_us=15000,
                    ui_avg_ms=28,
                    ui_p95_ms=35,
                    ui_max_ms=42,
                    touch_hz=100,
                    app_free_bytes=1048576,
                    safe_mode=False,
                    reset_reason=1,
                ),
                log_lines=[
                    "[BOOT] FreeRTOS dual-core tasks initialized",
                    "[BLE] Connected to phone central MTU=247",
                    "[LVGL] Landscape display ready 320x240",
                ],
                log_total_lines=3,
            ),
            1022,
            pb.EF_IS_RESPONSE,
            0,
        ),
    ]

    header_entries = []

    for test_id, msg_type_val, msg_type_name, desc, msg_obj, seq_id, flags, lane in vectors:
        raw_pb = msg_obj.SerializeToString()
        framed = create_frame(msg_type_val, seq_id, flags, raw_pb)
        frags_23 = fragment_frame(framed, lane, 23)
        frags_247 = fragment_frame(framed, lane, 247)

        # Write .bin
        bin_path = FIXTURES_DIR / f"{test_id}.bin"
        bin_path.write_bytes(raw_pb)

        # Write .frame
        frame_path = FIXTURES_DIR / f"{test_id}.frame"
        frame_path.write_bytes(framed)

        # Compute CRC of content (header + payload)
        content_crc = calc_crc32(framed[:-4])

        # Write .json
        fixture_doc = {
            "test_id": test_id,
            "message_type": msg_type_val,
            "message_name": msg_type_name,
            "description": desc,
            "json_payload": MessageToDict(
                msg_obj, preserving_proto_field_name=True
            ),
            "protobuf_bytes_hex": raw_pb.hex(),
            "protobuf_bytes_len": len(raw_pb),
            "envelope": {
                "magic_hex": f"0x{FRAME_MAGIC:04X}",
                "version": FRAME_VERSION,
                "flags": flags,
                "msg_type": msg_type_val,
                "msg_id": seq_id,
                "payload_len": len(raw_pb),
                "crc32_hex": f"0x{content_crc:08X}",
                "framed_bytes_hex": framed.hex(),
                "framed_bytes_len": len(framed),
            },
            "fragments_mtu23": [f.hex() for f in frags_23],
            "fragments_mtu247": [f.hex() for f in frags_247],
        }

        json_path = FIXTURES_DIR / f"{test_id}.json"
        json_path.write_text(json.dumps(fixture_doc, indent=2), encoding="utf-8")
        print(f"Generated {test_id} ({msg_type_name}, pb={len(raw_pb)}B, frame={len(framed)}B)")

        # Prepare C header entry
        pb_hex_c = ", ".join(f"0x{b:02x}" for b in raw_pb) or "0"
        frame_hex_c = ", ".join(f"0x{b:02x}" for b in framed)
        header_entries.append(
            f'    {{ "{test_id}", {msg_type_val}, {seq_id}, {flags}, {lane}, {len(raw_pb)}, '
            f'{{ {pb_hex_c} }}, {len(framed)}, {{ {frame_hex_c} }} }}'
        )

    # Generate fixtures_data.h
    num_vecs = len(vectors)
    entries_str = ",\n".join(header_entries)
    header_content = f"""#ifndef DASHCUE_PROTOCOL_FIXTURES_DATA_H
#define DASHCUE_PROTOCOL_FIXTURES_DATA_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {{
#endif

typedef struct {{
    const char *test_id;
    uint16_t msg_type;
    uint32_t msg_id;
    uint8_t flags;
    uint8_t lane;
    size_t pb_len;
    uint8_t pb_bytes[1100];
    size_t frame_len;
    uint8_t frame_bytes[1150];
}} GoldenFixture;

#define DASHCUE_NUM_GOLDEN_FIXTURES {num_vecs}

static const GoldenFixture GOLDEN_FIXTURES[DASHCUE_NUM_GOLDEN_FIXTURES] = {{
{entries_str}
}};

#ifdef __cplusplus
}}
#endif

#endif // DASHCUE_PROTOCOL_FIXTURES_DATA_H
"""
    (FIXTURES_DIR / "fixtures_data.h").write_text(header_content, encoding="utf-8")
    print(f"Generated {FIXTURES_DIR / 'fixtures_data.h'} with {len(vectors)} golden vectors.")
    print("All golden fixtures successfully generated!")


if __name__ == "__main__":
    main()
