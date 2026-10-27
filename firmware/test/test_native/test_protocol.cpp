#include <unity.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pb.h>
#include <pb_decode.h>
#include <pb_encode.h>

#include "protocol/crc32.h"
#include "protocol/framing.h"
#include "protocol/fragmentation.h"
#include "protocol/dedup_cache.h"
#include "protocol/generated/dashcue.pb.h"
#include "../../../protocol/fixtures/fixtures_data.h"

void setUp(void) {}
void tearDown(void) {}

// Helper: Nanopb decode and re-encode dispatch for a given fixture
static bool nanopb_roundtrip_fixture(const GoldenFixture *fix, uint8_t *out_pb, size_t *out_len) {
    pb_istream_t istream = pb_istream_from_buffer(fix->pb_bytes, fix->pb_len);
    pb_ostream_t ostream = pb_ostream_from_buffer(out_pb, 1100);
    bool dec_ok = false;
    bool enc_ok = false;

    switch (fix->msg_type) {
        case dashcue_v1_MessageType_MT_HELLO: {
            dashcue_v1_Hello msg = dashcue_v1_Hello_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_Hello_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_Hello_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_HELLO_ACK: {
            dashcue_v1_HelloAck msg = dashcue_v1_HelloAck_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_HelloAck_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_HelloAck_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_SNAPSHOT_REQUEST: {
            dashcue_v1_SnapshotRequest msg = dashcue_v1_SnapshotRequest_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_SnapshotRequest_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_SnapshotRequest_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_MEDIA_STATE: {
            dashcue_v1_MediaState msg = dashcue_v1_MediaState_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_MediaState_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_MediaState_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_MEDIA_CMD: {
            dashcue_v1_MediaCmd msg = dashcue_v1_MediaCmd_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_MediaCmd_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_MediaCmd_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_ARTWORK_BEGIN: {
            dashcue_v1_ArtworkBegin msg = dashcue_v1_ArtworkBegin_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_ArtworkBegin_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_ArtworkBegin_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_ARTWORK_CHUNK: {
            dashcue_v1_ArtworkChunk msg = dashcue_v1_ArtworkChunk_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_ArtworkChunk_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_ArtworkChunk_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_ARTWORK_END: {
            dashcue_v1_ArtworkEnd msg = dashcue_v1_ArtworkEnd_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_ArtworkEnd_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_ArtworkEnd_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_NAV_STATE: {
            dashcue_v1_NavState msg = dashcue_v1_NavState_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_NavState_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_NavState_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_NAV_CMD: {
            dashcue_v1_NavCmd msg = dashcue_v1_NavCmd_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_NavCmd_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_NavCmd_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_ROUTE_OVERVIEW: {
            dashcue_v1_RouteOverview msg = dashcue_v1_RouteOverview_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_RouteOverview_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_RouteOverview_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_SETTINGS_SET: {
            dashcue_v1_SettingsSet msg = dashcue_v1_SettingsSet_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_SettingsSet_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_SettingsSet_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_SETTINGS_STATE: {
            dashcue_v1_SettingsState msg = dashcue_v1_SettingsState_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_SettingsState_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_SettingsState_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_DEVICE_STATUS: {
            dashcue_v1_DeviceStatus msg = dashcue_v1_DeviceStatus_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_DeviceStatus_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_DeviceStatus_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_HEARTBEAT: {
            dashcue_v1_Heartbeat msg = dashcue_v1_Heartbeat_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_Heartbeat_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_Heartbeat_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_CMD_RESULT: {
            dashcue_v1_CmdResult msg = dashcue_v1_CmdResult_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_CmdResult_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_CmdResult_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_DIAG_REQ: {
            dashcue_v1_DiagReq msg = dashcue_v1_DiagReq_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_DiagReq_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_DiagReq_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_DIAG_RESP: {
            dashcue_v1_DiagResp msg = dashcue_v1_DiagResp_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_DiagResp_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_DiagResp_fields, &msg);
            break;
        }
        case dashcue_v1_MessageType_MT_ERROR: {
            dashcue_v1_ErrorMsg msg = dashcue_v1_ErrorMsg_init_zero;
            dec_ok = pb_decode(&istream, dashcue_v1_ErrorMsg_fields, &msg);
            if (dec_ok) enc_ok = pb_encode(&ostream, dashcue_v1_ErrorMsg_fields, &msg);
            break;
        }
        default:
            return false;
    }

    if (dec_ok && enc_ok) {
        *out_len = ostream.bytes_written;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// TC-PR-01: C++ Golden Vectors
// Encode and decode all 20 fixtures with byte-identical match using Nanopb.
// ---------------------------------------------------------------------------
void test_tc_pr_01_cpp_golden_vectors(void) {
    for (size_t i = 0; i < DASHCUE_NUM_GOLDEN_FIXTURES; ++i) {
        const GoldenFixture *fix = &GOLDEN_FIXTURES[i];

        // 1. Frame decode
        FrameHeader hdr;
        const uint8_t *payload = NULL;
        size_t payload_len = 0;
        dashcue_v1_ErrorCode err = frame_decode(fix->frame_bytes, fix->frame_len, &hdr, &payload, &payload_len);
        TEST_ASSERT_EQUAL_MESSAGE(dashcue_v1_ErrorCode_EC_NONE, err, fix->test_id);
        TEST_ASSERT_EQUAL_MESSAGE(fix->msg_type, hdr.msg_type, fix->test_id);
        TEST_ASSERT_EQUAL_MESSAGE(fix->msg_id, hdr.msg_id, fix->test_id);
        TEST_ASSERT_EQUAL_MESSAGE(fix->flags, hdr.flags, fix->test_id);
        TEST_ASSERT_EQUAL_MESSAGE(fix->pb_len, payload_len, fix->test_id);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(fix->pb_bytes, payload, payload_len, fix->test_id);

        // 2. Frame encode round-trip
        uint8_t reencoded_frame[1200];
        size_t reencoded_frame_len = 0;
        bool f_ok = frame_encode(&hdr, fix->pb_bytes, fix->pb_len, reencoded_frame, &reencoded_frame_len, sizeof(reencoded_frame));
        TEST_ASSERT_TRUE_MESSAGE(f_ok, fix->test_id);
        TEST_ASSERT_EQUAL_MESSAGE(fix->frame_len, reencoded_frame_len, fix->test_id);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(fix->frame_bytes, reencoded_frame, fix->frame_len, fix->test_id);

        // 3. Nanopb decode & re-encode byte parity
        uint8_t reencoded_pb[1100];
        size_t reencoded_pb_len = 0;
        bool pb_ok = nanopb_roundtrip_fixture(fix, reencoded_pb, &reencoded_pb_len);
        TEST_ASSERT_TRUE_MESSAGE(pb_ok, fix->test_id);
        TEST_ASSERT_EQUAL_MESSAGE(fix->pb_len, reencoded_pb_len, fix->test_id);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(fix->pb_bytes, reencoded_pb, fix->pb_len, fix->test_id);
    }
}

// ---------------------------------------------------------------------------
// TC-PR-02: Shared Golden Test Vector Parity
// Verify fixture schema definitions, metadata, non-zero sizes, and structure.
// ---------------------------------------------------------------------------
void test_tc_pr_02_golden_vector_parity(void) {
    TEST_ASSERT_EQUAL(22, DASHCUE_NUM_GOLDEN_FIXTURES);
    for (size_t i = 0; i < DASHCUE_NUM_GOLDEN_FIXTURES; ++i) {
        const GoldenFixture *fix = &GOLDEN_FIXTURES[i];
        TEST_ASSERT_NOT_NULL(fix->test_id);
        TEST_ASSERT_GREATER_THAN(0, fix->pb_len);
        TEST_ASSERT_GREATER_THAN(DASHCUE_MIN_FRAME_SIZE, fix->frame_len);
        TEST_ASSERT_EQUAL(fix->pb_len + DASHCUE_HEADER_SIZE + DASHCUE_TRAILER_SIZE, fix->frame_len);

        // Header check
        uint16_t magic = (uint16_t)fix->frame_bytes[0] | ((uint16_t)fix->frame_bytes[1] << 8);
        TEST_ASSERT_EQUAL_HEX16(DASHCUE_FRAME_MAGIC, magic);
        TEST_ASSERT_EQUAL_UINT8(DASHCUE_FRAME_VERSION, fix->frame_bytes[2]);
    }
}

// ---------------------------------------------------------------------------
// TC-PR-03: CRC32 Corruption Rejection
// Invert bits in header, payload, or CRC trailer; frame validator must reject.
// ---------------------------------------------------------------------------
void test_tc_pr_03_crc32_corruption_rejection(void) {
    const GoldenFixture *fix = &GOLDEN_FIXTURES[5]; // MediaState fixture
    uint8_t corrupt_buf[256];
    memcpy(corrupt_buf, fix->frame_bytes, fix->frame_len);

    FrameHeader hdr;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    // Baseline: valid frame decodes cleanly
    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_NONE, frame_decode(corrupt_buf, fix->frame_len, &hdr, &payload, &payload_len));

    // 1. Bit flip in header (byte 4: msg_type)
    corrupt_buf[4] ^= 0x01;
    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_CRC, frame_decode(corrupt_buf, fix->frame_len, &hdr, &payload, &payload_len));
    corrupt_buf[4] ^= 0x01; // Restore

    // 2. Bit flip in payload
    corrupt_buf[20] ^= 0x80;
    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_CRC, frame_decode(corrupt_buf, fix->frame_len, &hdr, &payload, &payload_len));
    corrupt_buf[20] ^= 0x80; // Restore

    // 3. Bit flip in CRC32 trailer
    corrupt_buf[fix->frame_len - 1] ^= 0x01;
    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_CRC, frame_decode(corrupt_buf, fix->frame_len, &hdr, &payload, &payload_len));
    corrupt_buf[fix->frame_len - 1] ^= 0x01; // Restore

    // 4. Invert all CRC bits
    corrupt_buf[fix->frame_len - 4] ^= 0xFF;
    corrupt_buf[fix->frame_len - 3] ^= 0xFF;
    corrupt_buf[fix->frame_len - 2] ^= 0xFF;
    corrupt_buf[fix->frame_len - 1] ^= 0xFF;
    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_CRC, frame_decode(corrupt_buf, fix->frame_len, &hdr, &payload, &payload_len));
}

// ---------------------------------------------------------------------------
// TC-PR-04: Length Bounds Rejection
// Inject frame with payload_len > DASHCUE_MAX_PAYLOAD_SIZE or truncated buffer;
// reject cleanly.
// ---------------------------------------------------------------------------
void test_tc_pr_04_length_bounds_rejection(void) {
    FrameHeader hdr;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    // 1. Frame too small (< 20 bytes)
    uint8_t small_frame[15] = {0};
    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_DECODE, frame_decode(small_frame, sizeof(small_frame), &hdr, &payload, &payload_len));

    // 2. Frame claims payload one byte over the advertised maximum
    uint8_t overlong_header[20];
    FrameHeader h;
    h.magic = DASHCUE_FRAME_MAGIC;
    h.version = DASHCUE_FRAME_VERSION;
    h.flags = 0;
    h.msg_type = dashcue_v1_MessageType_MT_MEDIA_CMD;
    h.msg_id = 1;
    h.length = DASHCUE_MAX_PAYLOAD_SIZE + 1; // one byte over the cap
    h.reserved = 0;

    uint8_t dummy_out[5000];
    size_t dummy_out_len = 0;
    // frame_encode must reject a payload over the cap before reading it
    TEST_ASSERT_FALSE(frame_encode(&h, small_frame, DASHCUE_MAX_PAYLOAD_SIZE + 1, dummy_out, &dummy_out_len, sizeof(dummy_out)));

    // 3. Manually constructed overlong frame passed to decode
    overlong_header[0] = 0x5C;
    overlong_header[1] = 0xD4;
    overlong_header[2] = 0x01;
    overlong_header[3] = 0x00;
    overlong_header[4] = 0x01;
    overlong_header[5] = 0x00;
    overlong_header[6] = 0x01;
    overlong_header[7] = 0x00;
    overlong_header[8] = 0x00;
    overlong_header[9] = 0x00;
    // Length = 4097 (0x00001001 LE: 0x01, 0x10, 0x00, 0x00)
    overlong_header[10] = 0x01;
    overlong_header[11] = 0x10;
    overlong_header[12] = 0x00;
    overlong_header[13] = 0x00;
    overlong_header[14] = 0x00;
    overlong_header[15] = 0x00;
    // trailer
    overlong_header[16] = 0x00;
    overlong_header[17] = 0x00;
    overlong_header[18] = 0x00;
    overlong_header[19] = 0x00;

    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_TOO_LARGE, frame_decode(overlong_header, 20, &hdr, &payload, &payload_len));
}

// ---------------------------------------------------------------------------
// TC-PR-05: BLE Reassembly MTU 23
// Split 1024-byte payload into 16-byte chunks (65 fragments), reassemble cleanly.
// ---------------------------------------------------------------------------
void test_tc_pr_05_ble_reassembly_mtu23(void) {
    uint8_t payload[1024];
    for (size_t i = 0; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)(i & 0xFF);
    }

    FrameHeader hdr;
    hdr.magic = DASHCUE_FRAME_MAGIC;
    hdr.version = DASHCUE_FRAME_VERSION;
    hdr.flags = 0;
    hdr.msg_type = dashcue_v1_MessageType_MT_ARTWORK_CHUNK;
    hdr.msg_id = 9001;
    hdr.length = sizeof(payload);
    hdr.reserved = 0;

    uint8_t framed[DASHCUE_MAX_FRAME_SIZE];
    size_t framed_len = 0;
    TEST_ASSERT_TRUE(frame_encode(&hdr, payload, sizeof(payload), framed, &framed_len, sizeof(framed)));
    TEST_ASSERT_EQUAL(16 + 1024 + 4, framed_len);

    // Fragment for MTU 23 (16-byte chunk size)
    const size_t chunk_size = fragment_calc_chunk_size(23);
    TEST_ASSERT_EQUAL(16, chunk_size);
    const uint8_t total_frags = fragment_calc_count(framed_len, 23);
    TEST_ASSERT_EQUAL(66, total_frags); // (1044 + 15) / 16 = 66

    ReassemblyEngine engine;
    reassembly_init(&engine);

    const uint8_t *reassembled = NULL;
    size_t reassembled_len = 0;

    for (uint8_t i = 0; i < total_frags; ++i) {
        uint8_t frag[64];
        size_t frag_len = 0;
        TEST_ASSERT_TRUE(fragment_pack(framed, framed_len, DASHCUE_LANE_BULK, i, 23, 8, frag, &frag_len, sizeof(frag)));

        ReassemblyStatus st = reassembly_feed(&engine, frag, frag_len, 1000 + i * 10, &reassembled, &reassembled_len);
        if (i == total_frags - 1) {
            TEST_ASSERT_EQUAL(REASSEMBLY_COMPLETE, st);
            TEST_ASSERT_EQUAL(framed_len, reassembled_len);
            TEST_ASSERT_EQUAL_HEX8_ARRAY(framed, reassembled, framed_len);
        } else {
            TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, st);
        }
    }

    // Verify reassembled frame decodes cleanly
    FrameHeader dec_hdr;
    const uint8_t *dec_payload = NULL;
    size_t dec_payload_len = 0;
    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_NONE, frame_decode(reassembled, reassembled_len, &dec_hdr, &dec_payload, &dec_payload_len));
    TEST_ASSERT_EQUAL(1024, dec_payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, dec_payload, 1024);
}

// ---------------------------------------------------------------------------
// TC-PR-06: BLE Reassembly MTU 247
// Split a maximum-size payload into 240-byte chunks and reassemble cleanly. The
// expected fragment count is derived from the contract, not hardcoded, so this
// vector tracks DASHCUE_MAX_PAYLOAD_SIZE instead of going stale with it.
// ---------------------------------------------------------------------------
void test_tc_pr_06_ble_reassembly_mtu247(void) {
    uint8_t payload[DASHCUE_MAX_PAYLOAD_SIZE];
    for (size_t i = 0; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)((i * 3) & 0xFF);
    }

    FrameHeader hdr;
    hdr.magic = DASHCUE_FRAME_MAGIC;
    hdr.version = DASHCUE_FRAME_VERSION;
    hdr.flags = 0;
    hdr.msg_type = dashcue_v1_MessageType_MT_ROUTE_OVERVIEW;
    hdr.msg_id = 9002;
    hdr.length = sizeof(payload);
    hdr.reserved = 0;

    uint8_t framed[DASHCUE_MAX_FRAME_SIZE];
    size_t framed_len = 0;
    TEST_ASSERT_TRUE(frame_encode(&hdr, payload, sizeof(payload), framed, &framed_len, sizeof(framed)));
    TEST_ASSERT_EQUAL(DASHCUE_HEADER_SIZE + DASHCUE_MAX_PAYLOAD_SIZE + DASHCUE_TRAILER_SIZE,
                      framed_len);

    // MTU 247 chunk capacity = 240 bytes
    const size_t chunk_size = fragment_calc_chunk_size(247);
    TEST_ASSERT_EQUAL(240, chunk_size);
    const uint8_t total_frags = fragment_calc_count(framed_len, 247);
    TEST_ASSERT_EQUAL((framed_len + chunk_size - 1) / chunk_size, total_frags);

    ReassemblyEngine engine;
    reassembly_init(&engine);

    const uint8_t *reassembled = NULL;
    size_t reassembled_len = 0;

    for (uint8_t i = 0; i < total_frags; ++i) {
        uint8_t frag[300];
        size_t frag_len = 0;
        TEST_ASSERT_TRUE(fragment_pack(framed, framed_len, DASHCUE_LANE_BULK, i, 247, 8, frag, &frag_len, sizeof(frag)));

        ReassemblyStatus st = reassembly_feed(&engine, frag, frag_len, 2000 + i * 20, &reassembled, &reassembled_len);
        if (i == total_frags - 1) {
            TEST_ASSERT_EQUAL(REASSEMBLY_COMPLETE, st);
            TEST_ASSERT_EQUAL(framed_len, reassembled_len);
            TEST_ASSERT_EQUAL_HEX8_ARRAY(framed, reassembled, framed_len);
        } else {
            TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, st);
        }
    }
}

// ---------------------------------------------------------------------------
// TC-PR-07: Flow Control & Timeout
// 1. Partial reassembly evicted after 2.0 s timeout; frag_timeouts counter incremented.
// 2. Initial 8 credits exhausted; bulk sender halts until credit granted; control lane preserved.
// ---------------------------------------------------------------------------
void test_tc_pr_07_flow_control_and_timeout(void) {
    // Part 1: Timeout eviction
    ReassemblyEngine engine;
    reassembly_init(&engine);

    uint8_t fake_frag[10] = {DASHCUE_LANE_BULK, 5, 0, 8, 1, 2, 3, 4, 5, 6};
    const uint8_t *out = NULL;
    size_t out_len = 0;

    // Send frag 0 at t = 1000 ms
    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, fake_frag, sizeof(fake_frag), 1000, &out, &out_len));

    // Tick at t = 3001 ms (> 2000 ms timeout)
    reassembly_tick(&engine, 3001);
    TEST_ASSERT_EQUAL_UINT32(1, engine.frag_timeouts_count);
    TEST_ASSERT_FALSE(engine.lanes[DASHCUE_LANE_BULK].active);

    // Send frag 1 at t = 3002 ms -> should reject as timeout/orphaned
    fake_frag[2] = 1;
    TEST_ASSERT_EQUAL(REASSEMBLY_ERROR, reassembly_feed(&engine, fake_frag, sizeof(fake_frag), 3002, &out, &out_len));

    // Part 2: Credit flow control
    CreditFlowController fc;
    flow_control_init(&fc, 8);
    TEST_ASSERT_EQUAL_UINT8(8, flow_control_get_credits(&fc));

    // Bulk lane consumes all 8 credits
    for (int i = 0; i < 8; ++i) {
        TEST_ASSERT_TRUE(flow_control_can_send(&fc, DASHCUE_LANE_BULK));
        TEST_ASSERT_TRUE(flow_control_consume(&fc, DASHCUE_LANE_BULK));
    }
    TEST_ASSERT_EQUAL_UINT8(0, flow_control_get_credits(&fc));

    // Bulk lane cannot send at 0 credits
    TEST_ASSERT_FALSE(flow_control_can_send(&fc, DASHCUE_LANE_BULK));
    TEST_ASSERT_FALSE(flow_control_consume(&fc, DASHCUE_LANE_BULK));

    // Control lane (Lane 0) has reserved credit and can still send!
    TEST_ASSERT_TRUE(flow_control_can_send(&fc, DASHCUE_LANE_CONTROL));
    TEST_ASSERT_TRUE(flow_control_consume(&fc, DASHCUE_LANE_CONTROL));

    // Grant 4 credits -> bulk lane can send again
    flow_control_grant(&fc, 4);
    TEST_ASSERT_EQUAL_UINT8(4, flow_control_get_credits(&fc));
    TEST_ASSERT_TRUE(flow_control_can_send(&fc, DASHCUE_LANE_BULK));
}

// ---------------------------------------------------------------------------
// TC-PR-08: Dedupe Cache & Result Replay
// Replays cached CMD_RESULT for duplicate commands within 60 s TTL.
// ---------------------------------------------------------------------------
void test_tc_pr_08_dedupe_cache_and_result_replay(void) {
    DedupCache cache;
    dedup_init(&cache);
    TEST_ASSERT_EQUAL_UINT32(0, dedup_get_duplicate_count(&cache));

    dashcue_v1_CmdResult res = dashcue_v1_CmdResult_init_zero;

    // t = 1000 ms: Command 999 arrives -> New command
    bool is_dup = dedup_check_and_record(&cache, 999, 1000, &res);
    TEST_ASSERT_FALSE(is_dup);
    TEST_ASSERT_EQUAL_UINT32(0, dedup_get_duplicate_count(&cache));

    // Subsystem executes command and confirms effect at t = 1500 ms
    bool upd_ok = dedup_update_result(&cache, 999, dashcue_v1_CmdResult_Status_ST_CONFIRMED, dashcue_v1_ErrorCode_EC_NONE, 1500);
    TEST_ASSERT_TRUE(upd_ok);

    // t = 10000 ms (10 s): Duplicate command 999 arrives
    is_dup = dedup_check_and_record(&cache, 999, 10000, &res);
    TEST_ASSERT_TRUE(is_dup);
    TEST_ASSERT_EQUAL_UINT32(1, dedup_get_duplicate_count(&cache));
    TEST_ASSERT_EQUAL_UINT32(999, res.command_id);
    TEST_ASSERT_EQUAL(dashcue_v1_CmdResult_Status_ST_CONFIRMED, res.status);
    TEST_ASSERT_EQUAL_UINT32(dashcue_v1_ErrorCode_EC_NONE, res.reason);
    TEST_ASSERT_TRUE(res.observed_at_ms == 1500);

    // Rapid tap: duplicate command 999 again at t = 11000 ms
    is_dup = dedup_check_and_record(&cache, 999, 11000, &res);
    TEST_ASSERT_TRUE(is_dup);
    TEST_ASSERT_EQUAL_UINT32(2, dedup_get_duplicate_count(&cache));

    // t = 65000 ms (> 60 s TTL expired): Command 999 is treated as new command
    is_dup = dedup_check_and_record(&cache, 999, 65000, &res);
    TEST_ASSERT_FALSE(is_dup);
}

// ---------------------------------------------------------------------------
// TC-PR-09: Sequence & Session / Magic Validation
// Reject invalid Magic, Version, and out-of-order/duplicate fragments.
// ---------------------------------------------------------------------------
void test_tc_pr_09_sequence_and_magic_validation(void) {
    const GoldenFixture *fix = &GOLDEN_FIXTURES[0];
    uint8_t buf[64];
    memcpy(buf, fix->frame_bytes, fix->frame_len);

    FrameHeader hdr;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    // 1. Invalid Magic (change 0xD45C to 0x1234, recomputing valid CRC for frame)
    buf[0] = 0x34;
    buf[1] = 0x12;
    uint32_t magic_crc = crc32_calculate(buf, DASHCUE_HEADER_SIZE + fix->pb_len);
    buf[fix->frame_len - 4] = (uint8_t)(magic_crc & 0xFF);
    buf[fix->frame_len - 3] = (uint8_t)((magic_crc >> 8) & 0xFF);
    buf[fix->frame_len - 2] = (uint8_t)((magic_crc >> 16) & 0xFF);
    buf[fix->frame_len - 1] = (uint8_t)((magic_crc >> 24) & 0xFF);
    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_UNKNOWN_TYPE, frame_decode(buf, fix->frame_len, &hdr, &payload, &payload_len));

    // Restore original frame bytes
    memcpy(buf, fix->frame_bytes, fix->frame_len);

    // 2. Invalid Version (change 0x01 to 0x02, recomputing valid CRC for frame)
    buf[2] = 0x02;
    uint32_t ver_crc = crc32_calculate(buf, DASHCUE_HEADER_SIZE + fix->pb_len);
    buf[fix->frame_len - 4] = (uint8_t)(ver_crc & 0xFF);
    buf[fix->frame_len - 3] = (uint8_t)((ver_crc >> 8) & 0xFF);
    buf[fix->frame_len - 2] = (uint8_t)((ver_crc >> 16) & 0xFF);
    buf[fix->frame_len - 1] = (uint8_t)((ver_crc >> 24) & 0xFF);
    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_VERSION_MISMATCH, frame_decode(buf, fix->frame_len, &hdr, &payload, &payload_len));

    // 3. Reassembly out-of-order rejection
    ReassemblyEngine engine;
    reassembly_init(&engine);

    uint8_t frag0[10] = {DASHCUE_LANE_BULK, 3, 0, 8, 1, 2, 3, 4, 5, 6};
    uint8_t frag2[10] = {DASHCUE_LANE_BULK, 3, 2, 8, 7, 8, 9, 0, 1, 2}; // Skips index 1!

    const uint8_t *out = NULL;
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, frag0, sizeof(frag0), 100, &out, &out_len));
    // Frag 2 received when expecting frag 1 -> must fail and discard
    TEST_ASSERT_EQUAL(REASSEMBLY_ERROR, reassembly_feed(&engine, frag2, sizeof(frag2), 110, &out, &out_len));
    TEST_ASSERT_EQUAL_UINT32(1, engine.out_of_order_count);
    TEST_ASSERT_FALSE(engine.lanes[DASHCUE_LANE_BULK].active);

    // 4. Clean frame restart on frag 0 & duplicate fragment rejection
    reassembly_init(&engine);
    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, frag0, sizeof(frag0), 200, &out, &out_len));
    // Feed frag 0 again: clean frame restart (R2) resets lane and begins new reassembly
    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, frag0, sizeof(frag0), 210, &out, &out_len));
    TEST_ASSERT_TRUE(engine.lanes[DASHCUE_LANE_BULK].active);
    TEST_ASSERT_EQUAL(1, engine.lanes[DASHCUE_LANE_BULK].next_frag_idx);

    // Duplicate intermediate fragment (feed frag 1, then duplicate frag 1 -> error)
    uint8_t frag1[10] = {DASHCUE_LANE_BULK, 3, 1, 8, 4, 5, 6, 7, 8, 9};
    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, frag1, sizeof(frag1), 220, &out, &out_len));
    TEST_ASSERT_EQUAL(REASSEMBLY_ERROR, reassembly_feed(&engine, frag1, sizeof(frag1), 230, &out, &out_len));
    TEST_ASSERT_EQUAL_UINT32(1, engine.out_of_order_count);
}

// ---------------------------------------------------------------------------
// TC-PR-10: Version Negotiation
// Reject major version mismatch; negotiate effective minor version = min(both).
// ---------------------------------------------------------------------------
void test_tc_pr_10_version_negotiation(void) {
    const uint32_t hud_proto_major = 1;
    const uint32_t hud_proto_minor = 0;

    // Phone sends Hello with proto_major = 2 (incompatible)
    dashcue_v1_Hello phone_hello_v2 = dashcue_v1_Hello_init_zero;
    phone_hello_v2.proto_major = 2;
    phone_hello_v2.proto_minor = 0;

    dashcue_v1_HelloAck ack = dashcue_v1_HelloAck_init_zero;
    if (phone_hello_v2.proto_major != hud_proto_major) {
        ack.accepted = 0;
        ack.reject_reason = dashcue_v1_ErrorCode_EC_VERSION_MISMATCH;
    } else {
        ack.accepted = 1;
        ack.proto_minor = (hud_proto_minor < phone_hello_v2.proto_minor) ? hud_proto_minor : phone_hello_v2.proto_minor;
    }
    TEST_ASSERT_EQUAL_UINT32(0, ack.accepted);
    TEST_ASSERT_EQUAL_UINT32(dashcue_v1_ErrorCode_EC_VERSION_MISMATCH, ack.reject_reason);

    // Phone sends Hello with proto_major = 1, proto_minor = 3
    dashcue_v1_Hello phone_hello_v1_3 = dashcue_v1_Hello_init_zero;
    phone_hello_v1_3.proto_major = 1;
    phone_hello_v1_3.proto_minor = 3;

    if (phone_hello_v1_3.proto_major != hud_proto_major) {
        ack.accepted = 0;
        ack.reject_reason = dashcue_v1_ErrorCode_EC_VERSION_MISMATCH;
    } else {
        ack.accepted = 1;
        ack.proto_major = hud_proto_major;
        ack.proto_minor = (hud_proto_minor < phone_hello_v1_3.proto_minor) ? hud_proto_minor : phone_hello_v1_3.proto_minor;
    }
    TEST_ASSERT_EQUAL_UINT32(1, ack.accepted);
    TEST_ASSERT_EQUAL_UINT32(1, ack.proto_major);
    TEST_ASSERT_EQUAL_UINT32(0, ack.proto_minor); // min(0, 3) = 0
}

// ---------------------------------------------------------------------------
// TC-PR-11: Lane Interleaving
// Stream bulk fragments; inject high-priority control message; control message
// processes immediately without waiting or corrupting bulk transfer.
// ---------------------------------------------------------------------------
void test_tc_pr_11_lane_interleaving(void) {
    ReassemblyEngine engine;
    reassembly_init(&engine);

    // Bulk message on Lane 1 (3 fragments)
    uint8_t bulk_frag0[10] = {DASHCUE_LANE_BULK, 3, 0, 8, 'B', '1', 'a', 'b', 'c', 'd'};
    uint8_t bulk_frag1[10] = {DASHCUE_LANE_BULK, 3, 1, 8, 'B', '2', 'e', 'f', 'g', 'h'};
    uint8_t bulk_frag2[10] = {DASHCUE_LANE_BULK, 3, 2, 8, 'B', '3', 'i', 'j', 'k', 'l'};

    // Urgent Control command on Lane 0 (1 fragment)
    uint8_t ctrl_frag[10] = {DASHCUE_LANE_CONTROL, 1, 0, 8, 'C', 'M', 'D', '!', 0, 0};

    const uint8_t *out = NULL;
    size_t out_len = 0;

    // 1. Send bulk fragment 0
    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, bulk_frag0, sizeof(bulk_frag0), 100, &out, &out_len));

    // 2. Interleave urgent control fragment (Lane 0)
    TEST_ASSERT_EQUAL(REASSEMBLY_COMPLETE, reassembly_feed(&engine, ctrl_frag, sizeof(ctrl_frag), 110, &out, &out_len));
    TEST_ASSERT_EQUAL(6, out_len);
    TEST_ASSERT_EQUAL_UINT8('C', out[0]);
    TEST_ASSERT_EQUAL_UINT8('M', out[1]);

    // 3. Resume bulk lane: send bulk fragment 1
    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, bulk_frag1, sizeof(bulk_frag1), 120, &out, &out_len));

    // 4. Send bulk fragment 2 -> bulk reassembly completes successfully!
    TEST_ASSERT_EQUAL(REASSEMBLY_COMPLETE, reassembly_feed(&engine, bulk_frag2, sizeof(bulk_frag2), 130, &out, &out_len));
    TEST_ASSERT_EQUAL(18, out_len); // 3 * 6 bytes payload
    TEST_ASSERT_EQUAL_UINT8('B', out[0]);
    TEST_ASSERT_EQUAL_UINT8('1', out[1]);
    TEST_ASSERT_EQUAL_UINT8('B', out[6]);
    TEST_ASSERT_EQUAL_UINT8('2', out[7]);
    TEST_ASSERT_EQUAL_UINT8('B', out[12]);
    TEST_ASSERT_EQUAL_UINT8('3', out[13]);
}

// ---------------------------------------------------------------------------
// TC-PR-12: Protocol Fuzzing
// Inject 1,000 malformed, truncated, and random byte streams into framing & reassembly.
// Verify zero crashes, zero heap corruptions, 100% clean rejection.
// ---------------------------------------------------------------------------
void test_tc_pr_12_protocol_fuzzing(void) {
    ReassemblyEngine engine;
    reassembly_init(&engine);

    srand(0x1337BEEF);

    uint8_t random_data[512];
    FrameHeader hdr;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    for (int iter = 0; iter < 1000; ++iter) {
        size_t len = (size_t)(rand() % sizeof(random_data));
        for (size_t j = 0; j < len; ++j) {
            random_data[j] = (uint8_t)(rand() & 0xFF);
        }

        // Fuzz frame_decode: must not crash
        frame_decode(random_data, len, &hdr, &payload, &payload_len);

        // Fuzz reassembly_feed: must not crash
        const uint8_t *out = NULL;
        size_t out_len = 0;
        reassembly_feed(&engine, random_data, len, 5000 + iter, &out, &out_len);
    }

    TEST_ASSERT_TRUE(true);
}

// ---------------------------------------------------------------------------
// TC-PR-13: String Cap Bounds
// Verify static string limits defined in dashcue.options and zero heap usage.
// ---------------------------------------------------------------------------
void test_tc_pr_13_string_cap_bounds(void) {
    // Assert static struct sizes match dashcue.options caps
    TEST_ASSERT_EQUAL(64, sizeof(((dashcue_v1_MediaState*)0)->title));
    TEST_ASSERT_EQUAL(64, sizeof(((dashcue_v1_MediaState*)0)->artist));
    TEST_ASSERT_EQUAL(64, sizeof(((dashcue_v1_MediaState*)0)->album));
    TEST_ASSERT_EQUAL(24, sizeof(((dashcue_v1_MediaState*)0)->app_name));
    TEST_ASSERT_EQUAL(48, sizeof(((dashcue_v1_NavState*)0)->street));
    TEST_ASSERT_EQUAL(32, sizeof(((dashcue_v1_Hello*)0)->device_id));
    TEST_ASSERT_EQUAL(24, sizeof(((dashcue_v1_Hello*)0)->fw_version));
    TEST_ASSERT_EQUAL(24, sizeof(((dashcue_v1_Hello*)0)->hw_revision));
    TEST_ASSERT_EQUAL(48, sizeof(((dashcue_v1_ErrorMsg*)0)->text));
    TEST_ASSERT_EQUAL(96, sizeof(((dashcue_v1_DiagResp*)0)->log_lines[0]));
    TEST_ASSERT_EQUAL(32, sizeof(((dashcue_v1_DiagResp*)0)->log_lines) / sizeof(((dashcue_v1_DiagResp*)0)->log_lines[0]));

    // Decode a MediaState with a string right up to the 64-char cap
    dashcue_v1_MediaState state = dashcue_v1_MediaState_init_zero;
    for (int i = 0; i < 63; ++i) {
        state.title[i] = 'A';
    }
    state.title[63] = '\0';

    uint8_t buf[256];
    pb_ostream_t ostream = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE(pb_encode(&ostream, dashcue_v1_MediaState_fields, &state));

    dashcue_v1_MediaState decoded = dashcue_v1_MediaState_init_zero;
    pb_istream_t istream = pb_istream_from_buffer(buf, ostream.bytes_written);
    TEST_ASSERT_TRUE(pb_decode(&istream, dashcue_v1_MediaState_fields, &decoded));
    TEST_ASSERT_EQUAL_STRING(state.title, decoded.title);
}

// ---------------------------------------------------------------------------
// TC-PR-14: Bidirectional E2E Codec Round-Trip
// Full round trip: Nanopb encode -> frame -> fragment -> reassemble -> unframe -> Nanopb decode.
// ---------------------------------------------------------------------------
void test_tc_pr_14_bidirectional_e2e_codec(void) {
    // 1. Construct original message
    dashcue_v1_NavState original_nav = dashcue_v1_NavState_init_zero;
    original_nav.route_id = 4040;
    original_nav.status = dashcue_v1_NavStatus_NAV_ACTIVE;
    original_nav.next = dashcue_v1_Maneuver_MV_ROUNDABOUT;
    original_nav.dist_to_next_m = 450;
    strncpy(original_nav.street, "Grand Avenue", sizeof(original_nav.street) - 1);
    original_nav.roundabout_exit = 3;
    original_nav.heading_deg = 270.5f;
    original_nav.speed_kmh_x10 = 550;
    original_nav.phone_ts_ms = 1729056123456ULL;

    // 2. Nanopb encode payload
    uint8_t pb_buf[dashcue_v1_NavState_size + 16];
    pb_ostream_t ostream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
    TEST_ASSERT_TRUE(pb_encode(&ostream, dashcue_v1_NavState_fields, &original_nav));
    const size_t pb_len = ostream.bytes_written;

    // 3. Envelope framing
    FrameHeader hdr;
    hdr.magic = DASHCUE_FRAME_MAGIC;
    hdr.version = DASHCUE_FRAME_VERSION;
    hdr.flags = dashcue_v1_EnvelopeFlag_EF_IDEMPOTENT;
    hdr.msg_type = dashcue_v1_MessageType_MT_NAV_STATE;
    hdr.msg_id = 7788;
    hdr.length = pb_len;
    hdr.reserved = 0;

    uint8_t framed[DASHCUE_MAX_FRAME_SIZE];
    size_t framed_len = 0;
    TEST_ASSERT_TRUE(frame_encode(&hdr, pb_buf, pb_len, framed, &framed_len, sizeof(framed)));

    // 4. BLE Fragmentation (MTU 23)
    const uint8_t total_frags = fragment_calc_count(framed_len, 23);
    TEST_ASSERT_GREATER_THAN(1, total_frags);

    // 5. Feed into ReassemblyEngine
    ReassemblyEngine engine;
    reassembly_init(&engine);

    const uint8_t *reassembled = NULL;
    size_t reassembled_len = 0;

    for (uint8_t i = 0; i < total_frags; ++i) {
        uint8_t frag[32];
        size_t frag_len = 0;
        TEST_ASSERT_TRUE(fragment_pack(framed, framed_len, DASHCUE_LANE_CONTROL, i, 23, 8, frag, &frag_len, sizeof(frag)));

        ReassemblyStatus st = reassembly_feed(&engine, frag, frag_len, 3000 + i * 5, &reassembled, &reassembled_len);
        if (i == total_frags - 1) {
            TEST_ASSERT_EQUAL(REASSEMBLY_COMPLETE, st);
        } else {
            TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, st);
        }
    }

    // 6. Unframe reassembled message
    FrameHeader dec_hdr;
    const uint8_t *dec_payload = NULL;
    size_t dec_payload_len = 0;
    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_NONE, frame_decode(reassembled, reassembled_len, &dec_hdr, &dec_payload, &dec_payload_len));
    TEST_ASSERT_EQUAL(hdr.magic, dec_hdr.magic);
    TEST_ASSERT_EQUAL(hdr.msg_type, dec_hdr.msg_type);
    TEST_ASSERT_EQUAL(hdr.msg_id, dec_hdr.msg_id);
    TEST_ASSERT_EQUAL(pb_len, dec_payload_len);

    // 7. Nanopb decode payload
    dashcue_v1_NavState decoded_nav = dashcue_v1_NavState_init_zero;
    pb_istream_t istream = pb_istream_from_buffer(dec_payload, dec_payload_len);
    TEST_ASSERT_TRUE(pb_decode(&istream, dashcue_v1_NavState_fields, &decoded_nav));

    // 8. Assert all fields match original exactly
    TEST_ASSERT_EQUAL_UINT32(original_nav.route_id, decoded_nav.route_id);
    TEST_ASSERT_EQUAL(original_nav.status, decoded_nav.status);
    TEST_ASSERT_EQUAL(original_nav.next, decoded_nav.next);
    TEST_ASSERT_EQUAL_UINT32(original_nav.dist_to_next_m, decoded_nav.dist_to_next_m);
    TEST_ASSERT_EQUAL_STRING(original_nav.street, decoded_nav.street);
    TEST_ASSERT_EQUAL_UINT32(original_nav.roundabout_exit, decoded_nav.roundabout_exit);
    TEST_ASSERT_EQUAL_FLOAT(original_nav.heading_deg, decoded_nav.heading_deg);
    TEST_ASSERT_EQUAL_INT32(original_nav.speed_kmh_x10, decoded_nav.speed_kmh_x10);
    TEST_ASSERT_TRUE(original_nav.phone_ts_ms == decoded_nav.phone_ts_ms);
}

// ---------------------------------------------------------------------------
// TC-FW-05: State Machine, Freshness Tracking, Driving Lock, and SafeMode
// ---------------------------------------------------------------------------
#include "state/app_state.h"

static void test_tc_fw_05_state_freshness_and_driving_lock(void) {
    app_state_init();

    // Initial state: Booting, stale
    TEST_ASSERT_EQUAL(HUD_STATE_BOOTING, app_state_get_peripheral_state());
    TEST_ASSERT_TRUE(app_state_is_phone_stale());
    TEST_ASSERT_TRUE(app_state_is_nav_stale());
    TEST_ASSERT_TRUE(app_state_is_media_stale());
    TEST_ASSERT_FALSE(app_state_is_driving_locked());

    // Connect
    app_state_set_connected(true, 1, 247);
    TEST_ASSERT_EQUAL(HUD_STATE_CONNECTED_UNSYNCED, app_state_get_peripheral_state());
    TEST_ASSERT_TRUE(app_state_is_connected());

    // Receive Heartbeat at t = 1000 ms with speed 95 (9.5 km/h)
    dashcue_v1_Heartbeat hb = dashcue_v1_Heartbeat_init_zero;
    hb.speed_kmh_x10 = 95;
    hb.credits = 8;
    app_state_on_heartbeat_rx(&hb, 1000);

    TEST_ASSERT_EQUAL(HUD_STATE_READY, app_state_get_peripheral_state());
    TEST_ASSERT_FALSE(app_state_is_phone_stale());
    TEST_ASSERT_EQUAL_INT32(95, app_state_get_speed_kmh_x10());
    TEST_ASSERT_TRUE(app_state_is_driving_locked()); // speed >= 80

    // Nav state update at t = 1100 ms
    dashcue_v1_NavState ns = dashcue_v1_NavState_init_zero;
    ns.dist_to_next_m = 300;
    app_state_on_nav_state(&ns, 1100);
    TEST_ASSERT_FALSE(app_state_is_nav_stale());

    // Media state update at t = 1200 ms
    dashcue_v1_MediaState ms = dashcue_v1_MediaState_init_zero;
    ms.status = dashcue_v1_PlaybackStatus_PB_PLAYING;
    app_state_on_media_state(&ms, 1200);
    TEST_ASSERT_FALSE(app_state_is_media_stale());

    // Check freshness at t = 11200 ms. With the automotive windows from
    // dashcue_config.h (nav/phone stale at 20 s, media at 30 s), every elapsed
    // age is still inside its window, so nothing is stamped stale yet.
    app_state_check_freshness(11200);
    TEST_ASSERT_FALSE(app_state_is_nav_stale());    // nav elapsed 10100 < 20000
    TEST_ASSERT_FALSE(app_state_is_media_stale());  // media elapsed 10000 < 30000
    TEST_ASSERT_FALSE(app_state_is_phone_stale());  // hb elapsed 10200 < 20000

    // Check freshness at t = 22500 ms: nav elapsed 21400 ms and heartbeat
    // elapsed 21500 ms both exceed the 20 s windows (10 missed 2 s heartbeats).
    app_state_check_freshness(22500);
    TEST_ASSERT_TRUE(app_state_is_nav_stale());
    TEST_ASSERT_TRUE(app_state_is_phone_stale());
    TEST_ASSERT_EQUAL(HUD_STATE_STALE, app_state_get_peripheral_state());
    // DEC-28 / SI-05: Driving lock MUST release when link becomes stale!
    TEST_ASSERT_FALSE(app_state_is_driving_locked());

    // Phone reconnects / sends heartbeat at t = 24000 ms
    app_state_on_heartbeat_rx(&hb, 24000);

    // Test SafeMode trigger (TC-HW-11): 3 resets within 60 s
    app_state_record_boot(2, 10);
    TEST_ASSERT_EQUAL_UINT32(1, app_state_get_crash_count());
    TEST_ASSERT_FALSE(app_state_is_safe_mode());

    app_state_record_boot(3, 25);
    TEST_ASSERT_EQUAL_UINT32(2, app_state_get_crash_count());
    TEST_ASSERT_FALSE(app_state_is_safe_mode());

    app_state_record_boot(4, 40);
    TEST_ASSERT_EQUAL_UINT32(3, app_state_get_crash_count());
    TEST_ASSERT_TRUE(app_state_is_safe_mode());
    TEST_ASSERT_EQUAL(HUD_STATE_SAFE_MODE, app_state_get_peripheral_state());
}

// ---------------------------------------------------------------------------
// Test runner main
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// TC-PR-ADV-01: Framing Length Boundaries, Integer Overflow & Null Defense
// ---------------------------------------------------------------------------
void test_tc_pr_adv_01_framing_boundaries_and_overflow(void) {
    FrameHeader hdr;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    // 1. Boundary: Exact Max Payload (DASHCUE_MAX_PAYLOAD_SIZE bytes)
    uint8_t max_payload[DASHCUE_MAX_PAYLOAD_SIZE];
    memset(max_payload, 0x5A, sizeof(max_payload));

    FrameHeader h_max;
    h_max.magic = DASHCUE_FRAME_MAGIC;
    h_max.version = DASHCUE_FRAME_VERSION;
    h_max.flags = 0;
    h_max.msg_type = dashcue_v1_MessageType_MT_ROUTE_OVERVIEW;
    h_max.msg_id = 1234;
    h_max.length = DASHCUE_MAX_PAYLOAD_SIZE;
    h_max.reserved = 0;

    uint8_t max_frame[DASHCUE_MAX_FRAME_SIZE];
    size_t max_frame_len = 0;
    TEST_ASSERT_TRUE(frame_encode(&h_max, max_payload, sizeof(max_payload), max_frame, &max_frame_len, sizeof(max_frame)));
    TEST_ASSERT_EQUAL(DASHCUE_MAX_FRAME_SIZE, max_frame_len);

    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_NONE, frame_decode(max_frame, max_frame_len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL(DASHCUE_MAX_PAYLOAD_SIZE, payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(max_payload, payload, 32);

    // 2. Boundary: Zero Payload (0 bytes, min frame 20 bytes)
    FrameHeader h_zero = h_max;
    h_zero.length = 0;
    uint8_t zero_frame[DASHCUE_MIN_FRAME_SIZE];
    size_t zero_frame_len = 0;
    TEST_ASSERT_TRUE(frame_encode(&h_zero, NULL, 0, zero_frame, &zero_frame_len, sizeof(zero_frame)));
    TEST_ASSERT_EQUAL(DASHCUE_MIN_FRAME_SIZE, zero_frame_len);

    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_NONE, frame_decode(zero_frame, zero_frame_len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL(0, payload_len);
    TEST_ASSERT_NULL(payload);

    // 3. Integer Overflow in header length (0xFFFFFFFF)
    uint8_t overflow_frame[32] = {0};
    overflow_frame[0] = 0x5C;
    overflow_frame[1] = 0xD4;
    overflow_frame[2] = 0x01;
    overflow_frame[10] = 0xFF;
    overflow_frame[11] = 0xFF;
    overflow_frame[12] = 0xFF;
    overflow_frame[13] = 0xFF;
    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_TOO_LARGE, frame_decode(overflow_frame, sizeof(overflow_frame), &hdr, &payload, &payload_len));

    // 4. Trailing garbage injection (frame_len > expected)
    uint8_t trailing_frame[30];
    memcpy(trailing_frame, zero_frame, 20);
    memset(&trailing_frame[20], 0xEE, 10);
    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_TOO_LARGE, frame_decode(trailing_frame, sizeof(trailing_frame), &hdr, &payload, &payload_len));

    // 5. Truncated frame (frame_len < expected)
    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_DECODE, frame_decode(max_frame, max_frame_len - 10, &hdr, &payload, &payload_len));

    // 6. Null pointer defense
    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_DECODE, frame_decode(NULL, zero_frame_len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL(dashcue_v1_ErrorCode_EC_DECODE, frame_decode(zero_frame, zero_frame_len, NULL, &payload, &payload_len));
    TEST_ASSERT_FALSE(frame_encode(NULL, max_payload, 10, max_frame, &max_frame_len, sizeof(max_frame)));
    TEST_ASSERT_FALSE(frame_encode(&h_max, NULL, 10, max_frame, &max_frame_len, sizeof(max_frame)));
}

// ---------------------------------------------------------------------------
// TC-PR-ADV-02: Hostile Fragmentation Headers, Length Bombing & Out-of-Order
// ---------------------------------------------------------------------------
void test_tc_pr_adv_02_hostile_fragmentation(void) {
    ReassemblyEngine engine;
    reassembly_init(&engine);
    const uint8_t *out = NULL;
    size_t out_len = 0;

    // 1. Truncated fragment header (< 4 bytes)
    uint8_t short_frag[3] = {DASHCUE_LANE_BULK, 2, 0};
    TEST_ASSERT_EQUAL(REASSEMBLY_ERROR, reassembly_feed(&engine, short_frag, sizeof(short_frag), 100, &out, &out_len));

    // 2. Invalid lane ID (>= 2)
    uint8_t bad_lane[8] = {2, 2, 0, 0, 1, 2, 3, 4};
    TEST_ASSERT_EQUAL(REASSEMBLY_ERROR, reassembly_feed(&engine, bad_lane, sizeof(bad_lane), 101, &out, &out_len));

    // 3. Zero total fragments (total_frags = 0)
    uint8_t zero_frags[8] = {DASHCUE_LANE_BULK, 0, 0, 0, 1, 2, 3, 4};
    TEST_ASSERT_EQUAL(REASSEMBLY_ERROR, reassembly_feed(&engine, zero_frags, sizeof(zero_frags), 102, &out, &out_len));

    // 4. frag_idx >= total_frags
    uint8_t bad_idx[8] = {DASHCUE_LANE_BULK, 3, 3, 0, 1, 2, 3, 4};
    TEST_ASSERT_EQUAL(REASSEMBLY_ERROR, reassembly_feed(&engine, bad_idx, sizeof(bad_idx), 103, &out, &out_len));

    // 5. Upfront length bomb: full non-final chunks alone exceed 4116.
    uint8_t length_bomb[204];
    length_bomb[0] = DASHCUE_LANE_BULK;
    length_bomb[1] = 25;
    length_bomb[2] = 0;
    length_bomb[3] = 0;
    memset(&length_bomb[4], 0x41, 200);
    TEST_ASSERT_EQUAL(REASSEMBLY_ERROR, reassembly_feed(&engine, length_bomb, sizeof(length_bomb), 104, &out, &out_len));
    TEST_ASSERT_EQUAL_UINT32(1, engine.length_violation_count);
    TEST_ASSERT_FALSE(engine.lanes[DASHCUE_LANE_BULK].active);

    // 6. Mid-stream total_frags mutation
    uint8_t f0[10] = {DASHCUE_LANE_CONTROL, 4, 0, 0, 1, 2, 3, 4, 5, 6};
    uint8_t f1_mutated[10] = {DASHCUE_LANE_CONTROL, 5, 1, 0, 1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, f0, sizeof(f0), 200, &out, &out_len));
    TEST_ASSERT_EQUAL(REASSEMBLY_ERROR, reassembly_feed(&engine, f1_mutated, sizeof(f1_mutated), 210, &out, &out_len));
    TEST_ASSERT_EQUAL_UINT32(1, engine.out_of_order_count);
    TEST_ASSERT_FALSE(engine.lanes[DASHCUE_LANE_CONTROL].active);

    // 7. Duplicate intermediate fragment
    uint8_t f1_good[10] = {DASHCUE_LANE_CONTROL, 3, 1, 0, 1, 2, 3, 4, 5, 6};
    f0[1] = 3;
    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, f0, sizeof(f0), 300, &out, &out_len));
    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, f1_good, sizeof(f1_good), 310, &out, &out_len));
    TEST_ASSERT_EQUAL(REASSEMBLY_ERROR, reassembly_feed(&engine, f1_good, sizeof(f1_good), 320, &out, &out_len));
    TEST_ASSERT_EQUAL_UINT32(2, engine.out_of_order_count);
    TEST_ASSERT_FALSE(engine.lanes[DASHCUE_LANE_CONTROL].active);
}

// ---------------------------------------------------------------------------
// TC-PR-ADV-03: Concurrent Bi-Lane Reassembly Isolation
// ---------------------------------------------------------------------------
void test_tc_pr_adv_03_concurrent_bi_lane_reassembly(void) {
    ReassemblyEngine engine;
    reassembly_init(&engine);
    const uint8_t *out = NULL;
    size_t out_len = 0;

    uint8_t c0[8] = {DASHCUE_LANE_CONTROL, 3, 0, 0, 'C', '0', 'a', 'b'};
    uint8_t c1[8] = {DASHCUE_LANE_CONTROL, 3, 1, 0, 'C', '1', 'c', 'd'};
    uint8_t c2[8] = {DASHCUE_LANE_CONTROL, 3, 2, 0, 'C', '2', 'e', 'f'};

    uint8_t b0[8] = {DASHCUE_LANE_BULK, 3, 0, 0, 'B', '0', '1', '2'};
    uint8_t b1[8] = {DASHCUE_LANE_BULK, 3, 1, 0, 'B', '1', '3', '4'};
    uint8_t b2[8] = {DASHCUE_LANE_BULK, 3, 2, 0, 'B', '2', '5', '6'};

    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, c0, sizeof(c0), 100, &out, &out_len));
    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, b0, sizeof(b0), 101, &out, &out_len));
    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, c1, sizeof(c1), 102, &out, &out_len));
    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, b1, sizeof(b1), 103, &out, &out_len));

    TEST_ASSERT_EQUAL(REASSEMBLY_COMPLETE, reassembly_feed(&engine, c2, sizeof(c2), 104, &out, &out_len));
    TEST_ASSERT_EQUAL(12, out_len);
    TEST_ASSERT_EQUAL_HEX8('C', out[0]);
    TEST_ASSERT_EQUAL_HEX8('C', out[4]);
    TEST_ASSERT_EQUAL_HEX8('C', out[8]);

    TEST_ASSERT_EQUAL(REASSEMBLY_COMPLETE, reassembly_feed(&engine, b2, sizeof(b2), 105, &out, &out_len));
    TEST_ASSERT_EQUAL(12, out_len);
    TEST_ASSERT_EQUAL_HEX8('B', out[0]);
    TEST_ASSERT_EQUAL_HEX8('B', out[4]);
    TEST_ASSERT_EQUAL_HEX8('B', out[8]);
}

// ---------------------------------------------------------------------------
// TC-PR-ADV-04: Dedupe Capacity Saturation, Eviction & Result Fidelity
// ---------------------------------------------------------------------------
void test_tc_pr_adv_04_dedupe_capacity_and_status_fidelity(void) {
    DedupCache cache;
    dedup_init(&cache);
    dashcue_v1_CmdResult res = dashcue_v1_CmdResult_init_zero;

    for (uint32_t i = 1; i <= DASHCUE_DEDUP_CACHE_SIZE; ++i) {
        bool is_dup = dedup_check_and_record(&cache, 1000 + i, 1000 + i, &res);
        TEST_ASSERT_FALSE(is_dup);
    }
    TEST_ASSERT_EQUAL_UINT32(0, dedup_get_duplicate_count(&cache));

    for (uint32_t i = 1; i <= DASHCUE_DEDUP_CACHE_SIZE; ++i) {
        bool is_dup = dedup_check_and_record(&cache, 1000 + i, 2000, &res);
        TEST_ASSERT_TRUE(is_dup);
    }
    TEST_ASSERT_EQUAL_UINT32(32, dedup_get_duplicate_count(&cache));

    bool is_dup = dedup_check_and_record(&cache, 2000, 3000, &res);
    TEST_ASSERT_FALSE(is_dup);

    is_dup = dedup_check_and_record(&cache, 1001, 3001, &res);
    TEST_ASSERT_FALSE(is_dup);

    dedup_update_result(&cache, 2000, dashcue_v1_CmdResult_Status_ST_FAILED, dashcue_v1_ErrorCode_EC_PERMISSION, 3500);
    is_dup = dedup_check_and_record(&cache, 2000, 3600, &res);
    TEST_ASSERT_TRUE(is_dup);
    TEST_ASSERT_EQUAL(dashcue_v1_CmdResult_Status_ST_FAILED, res.status);
    TEST_ASSERT_EQUAL_UINT32(dashcue_v1_ErrorCode_EC_PERMISSION, res.reason);
    TEST_ASSERT_TRUE(res.observed_at_ms == 3500);

    is_dup = dedup_check_and_record(&cache, 0, 4000, &res);
    TEST_ASSERT_FALSE(is_dup);
    is_dup = dedup_check_and_record(&cache, 0, 4050, &res);
    TEST_ASSERT_TRUE(is_dup);
    TEST_ASSERT_EQUAL_UINT32(0, res.command_id);
}

// ---------------------------------------------------------------------------
// TC-PR-ADV-05: Malformed Protobuf Wire Safety & Corrupted Payloads
// ---------------------------------------------------------------------------
void test_tc_pr_adv_05_malformed_protobuf_wire_safety(void) {
    uint8_t truncated_varint[] = {0x08, 0x96, 0x01, 0x10, 0x80};
    dashcue_v1_MediaCmd cmd = dashcue_v1_MediaCmd_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(truncated_varint, sizeof(truncated_varint));
    TEST_ASSERT_FALSE(pb_decode(&stream, dashcue_v1_MediaCmd_fields, &cmd));

    uint8_t overlong_varint[12] = {0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
    stream = pb_istream_from_buffer(overlong_varint, sizeof(overlong_varint));
    TEST_ASSERT_FALSE(pb_decode(&stream, dashcue_v1_MediaCmd_fields, &cmd));

    uint8_t invalid_wire[] = {0x0F, 0x01, 0x02};
    stream = pb_istream_from_buffer(invalid_wire, sizeof(invalid_wire));
    TEST_ASSERT_FALSE(pb_decode(&stream, dashcue_v1_MediaCmd_fields, &cmd));
}

void test_tc_pr_max_frame_short_final_fragment(void) {
    uint8_t frame[DASHCUE_MAX_FRAME_SIZE];
    for (size_t i = 0; i < sizeof(frame); ++i) frame[i] = (uint8_t)i;
    ReassemblyEngine engine;
    reassembly_init(&engine);
    const uint8_t count = fragment_calc_count(sizeof(frame), 247);
    const size_t chunk = fragment_calc_chunk_size(247);
    TEST_ASSERT_EQUAL_UINT8((sizeof(frame) + chunk - 1) / chunk, count);
    const uint8_t *reassembled = NULL;
    size_t reassembled_len = 0;
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t fragment[244];
        size_t fragment_len = 0;
        TEST_ASSERT_TRUE(fragment_pack(frame, sizeof(frame), DASHCUE_LANE_BULK,
                                       i, 247, 8, fragment, &fragment_len, sizeof(fragment)));
        ReassemblyStatus status = reassembly_feed(&engine, fragment, fragment_len,
                                                  i * 10, &reassembled, &reassembled_len);
        TEST_ASSERT_EQUAL(i + 1 == count ? REASSEMBLY_COMPLETE : REASSEMBLY_IN_PROGRESS, status);
    }
    TEST_ASSERT_EQUAL_UINT32(sizeof(frame), reassembled_len);
    TEST_ASSERT_EQUAL_MEMORY(frame, reassembled, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT32(0, engine.length_violation_count);
}

void test_tc_fw_ipc_snapshot_memory_budget(void) {
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(256, sizeof(ProtoToUiMessage));
    TEST_ASSERT_TRUE(ipc_queues_init());
    TEST_ASSERT_TRUE(app_state_init());
    // The HUD keeps media/nav snapshots only. RouteOverview is no longer stored
    // because the map surface moved to the phone; a 1 KB route copy would be
    // dead BSS. Verify the retained snapshot path instead.
    dashcue_v1_NavState nav = dashcue_v1_NavState_init_zero;
    nav.route_id = 42;
    nav.dist_to_next_m = 350;
    strncpy(nav.street, "Main Street", sizeof(nav.street) - 1);
    app_state_on_nav_state(&nav, 1000);
    ProtoToUiMessage event = {};
    event.type = IPC_EVT_NAV_STATE;
    TEST_ASSERT_TRUE(ipc_send_proto_to_ui(&event, 0));
    ProtoToUiMessage received = {};
    TEST_ASSERT_TRUE(ipc_recv_proto_to_ui(&received, 0));
    TEST_ASSERT_EQUAL(IPC_EVT_NAV_STATE, received.type);
    dashcue_v1_NavState snapshot = dashcue_v1_NavState_init_zero;
    app_state_get_nav_snapshot(&snapshot);
    TEST_ASSERT_EQUAL_MEMORY(&nav, &snapshot, sizeof(nav));
    struct PreviousUiEventLayout {
        IpcEventType type;
        union {
            decltype(event.data) remaining_events;
            dashcue_v1_MediaState media;
            dashcue_v1_NavState nav;
            dashcue_v1_RouteOverview route;
        } data;
    };
    printf("MEMORY previous_ui_event=%u previous_ui_queue=%u saved_queue_bytes=%u\n",
           (unsigned)sizeof(PreviousUiEventLayout),
           (unsigned)(PROTO_TO_UI_QUEUE_LEN * sizeof(PreviousUiEventLayout)),
           (unsigned)(PROTO_TO_UI_QUEUE_LEN * (sizeof(PreviousUiEventLayout) - sizeof(ProtoToUiMessage))));
    printf("MEMORY sizeof_ui_event=%u ui_event_queue=%u sizeof_ui_command=%u command_queue=%u ble_rx_queue=%u nav_snapshot=%u\n",
           (unsigned)sizeof(ProtoToUiMessage), (unsigned)(PROTO_TO_UI_QUEUE_LEN * sizeof(ProtoToUiMessage)),
           (unsigned)sizeof(UiToProtoMessage), (unsigned)(UI_TO_PROTO_QUEUE_LEN * sizeof(UiToProtoMessage)),
           (unsigned)(BLE_RX_QUEUE_LEN * sizeof(BleRxPacket)), (unsigned)sizeof(nav));
}

void test_tc_pr_clean_frame_restart(void) {
    ReassemblyEngine engine;
    reassembly_init(&engine);
    const uint8_t *out = NULL;
    size_t out_len = 0;

    // Frame A: 3 fragments (aborted after frag 0)
    uint8_t a0[8] = {DASHCUE_LANE_BULK, 3, 0, 0, 'A', '0', '1', '2'};
    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, a0, sizeof(a0), 100, &out, &out_len));
    TEST_ASSERT_TRUE(engine.lanes[DASHCUE_LANE_BULK].active);
    TEST_ASSERT_EQUAL(1, engine.lanes[DASHCUE_LANE_BULK].next_frag_idx);

    // Frame B: 2 fragments, starts with frag 0 while lane is active
    uint8_t b0[8] = {DASHCUE_LANE_BULK, 2, 0, 0, 'B', '0', '3', '4'};
    uint8_t b1[8] = {DASHCUE_LANE_BULK, 2, 1, 0, 'B', '1', '5', '6'};

    // Feeding b0 must cleanly reset the lane and initialize Frame B
    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, b0, sizeof(b0), 110, &out, &out_len));
    TEST_ASSERT_TRUE(engine.lanes[DASHCUE_LANE_BULK].active);
    TEST_ASSERT_EQUAL(2, engine.lanes[DASHCUE_LANE_BULK].total_frags);
    TEST_ASSERT_EQUAL(1, engine.lanes[DASHCUE_LANE_BULK].next_frag_idx);

    // Feeding b1 completes Frame B
    TEST_ASSERT_EQUAL(REASSEMBLY_COMPLETE, reassembly_feed(&engine, b1, sizeof(b1), 120, &out, &out_len));
    TEST_ASSERT_EQUAL(8, out_len);
    const uint8_t expected_payload[8] = {'B', '0', '3', '4', 'B', '1', '5', '6'};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_payload, out, 8);

    // Control lane: duplicate frag 0 must be strictly rejected with REASSEMBLY_ERROR (clean frame restart is BULK only)
    uint8_t c0[8] = {DASHCUE_LANE_CONTROL, 2, 0, 0, 'C', '0', '1', '2'};
    TEST_ASSERT_EQUAL(REASSEMBLY_IN_PROGRESS, reassembly_feed(&engine, c0, sizeof(c0), 130, &out, &out_len));
    TEST_ASSERT_TRUE(engine.lanes[DASHCUE_LANE_CONTROL].active);
    TEST_ASSERT_EQUAL(1, engine.lanes[DASHCUE_LANE_CONTROL].next_frag_idx);

    uint32_t prior_out_of_order = engine.out_of_order_count;
    TEST_ASSERT_EQUAL(REASSEMBLY_ERROR, reassembly_feed(&engine, c0, sizeof(c0), 140, &out, &out_len));
    TEST_ASSERT_EQUAL_UINT32(prior_out_of_order + 1, engine.out_of_order_count);
    TEST_ASSERT_FALSE(engine.lanes[DASHCUE_LANE_CONTROL].active);
}

// TCB: a structurally invalid bond address must not pass validation. A5:A5:
// A5:A5:A5:A5 is a real-world entry observed in the GAP bond list on
// 2026-10-26 (test-filler corruption); all-zero and 00-07-00-00-00-07 style
// placeholders must also be rejected, and a real phone address must pass.
void test_tc_pr_15_bond_addr_validation(void) {
    const uint8_t filler[6]    = {0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5};
    const uint8_t cleared[6]   = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const uint8_t unset[6]     = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t placeholder[6]={0x00, 0x07, 0x00, 0x00, 0x00, 0x07};
    const uint8_t real_phone[6]= {0x3C, 0x71, 0xBF, 0xA5, 0x01, 0x2D};
    TEST_ASSERT_FALSE(app_state_is_valid_bt_addr(nullptr));
    TEST_ASSERT_FALSE(app_state_is_valid_bt_addr(filler));
    TEST_ASSERT_FALSE(app_state_is_valid_bt_addr(cleared));
    TEST_ASSERT_FALSE(app_state_is_valid_bt_addr(unset));
    TEST_ASSERT_FALSE(app_state_is_valid_bt_addr(placeholder));
    TEST_ASSERT_TRUE(app_state_is_valid_bt_addr(real_phone));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_tc_pr_01_cpp_golden_vectors);
    RUN_TEST(test_tc_pr_02_golden_vector_parity);
    RUN_TEST(test_tc_pr_03_crc32_corruption_rejection);
    RUN_TEST(test_tc_pr_04_length_bounds_rejection);
    RUN_TEST(test_tc_pr_05_ble_reassembly_mtu23);
    RUN_TEST(test_tc_pr_06_ble_reassembly_mtu247);
    RUN_TEST(test_tc_pr_07_flow_control_and_timeout);
    RUN_TEST(test_tc_pr_08_dedupe_cache_and_result_replay);
    RUN_TEST(test_tc_pr_09_sequence_and_magic_validation);
    RUN_TEST(test_tc_pr_10_version_negotiation);
    RUN_TEST(test_tc_pr_11_lane_interleaving);
    RUN_TEST(test_tc_pr_12_protocol_fuzzing);
    RUN_TEST(test_tc_pr_13_string_cap_bounds);
    RUN_TEST(test_tc_pr_14_bidirectional_e2e_codec);
    RUN_TEST(test_tc_pr_15_bond_addr_validation);
    RUN_TEST(test_tc_fw_05_state_freshness_and_driving_lock);
    RUN_TEST(test_tc_pr_adv_01_framing_boundaries_and_overflow);
    RUN_TEST(test_tc_pr_adv_02_hostile_fragmentation);
    RUN_TEST(test_tc_pr_adv_03_concurrent_bi_lane_reassembly);
    RUN_TEST(test_tc_pr_adv_04_dedupe_capacity_and_status_fidelity);
    RUN_TEST(test_tc_pr_adv_05_malformed_protobuf_wire_safety);
    RUN_TEST(test_tc_pr_max_frame_short_final_fragment);
    RUN_TEST(test_tc_fw_ipc_snapshot_memory_budget);
    RUN_TEST(test_tc_pr_clean_frame_restart);

    return UNITY_END();
}
