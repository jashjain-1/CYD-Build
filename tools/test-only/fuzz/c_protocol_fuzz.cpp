#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "protocol/crc32.h"
#include "protocol/framing.h"
#include "protocol/fragmentation.h"
#include "protocol/dedup_cache.h"
#include "protocol/generated/dashcue.pb.h"
#include "../../../protocol/fixtures/fixtures_data.h"

// ANSI Color formatting for CLI evidence
#define ANSI_GREEN  "\033[32m"
#define ANSI_RED    "\033[31m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_RESET  "\033[0m"

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;
static int g_vulnerabilities_found = 0;
static unsigned int g_fuzz_iterations = 1000000;

#define FUZZ_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        printf(ANSI_RED "  [FAIL] %s:%d: %s" ANSI_RESET "\n", __FILE__, __LINE__, msg); \
    } \
} while(0)

#define RECORD_VULNERABILITY(id, title, desc) do { \
    g_vulnerabilities_found++; \
    g_tests_run++; \
    g_tests_failed++; \
    printf(ANSI_YELLOW "  [VULNERABILITY %s] %s: %s" ANSI_RESET "\n", id, title, desc); \
} while(0)

// ---------------------------------------------------------------------------
// Suite 1: CRC32 Fuzzing & Mutation Rejection
// ---------------------------------------------------------------------------
void fuzz_crc32_corruption(void) {
    printf(ANSI_CYAN "\n=== [SUITE 1] CRC32 Corruption & Bit-Flipping ===" ANSI_RESET "\n");

    const GoldenFixture *fix = &GOLDEN_FIXTURES[0]; // VEC-01-HELLO-MIN
    uint8_t corrupt_buf[256];
    FrameHeader hdr;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    // Baseline sanity check
    memcpy(corrupt_buf, fix->frame_bytes, fix->frame_len);
    dashcue_v1_ErrorCode err = frame_decode(corrupt_buf, fix->frame_len, &hdr, &payload, &payload_len);
    FUZZ_ASSERT(err == dashcue_v1_ErrorCode_EC_NONE, "Baseline golden fixture must decode cleanly");

    // 1. Bit flip across header bytes 0..15
    int header_crc_mismatches = 0;
    int header_premature_type_checks = 0;

    for (size_t b = 0; b < DASHCUE_HEADER_SIZE; ++b) {
        for (int bit = 0; bit < 8; ++bit) {
            memcpy(corrupt_buf, fix->frame_bytes, fix->frame_len);
            corrupt_buf[b] ^= (1u << bit);

            err = frame_decode(corrupt_buf, fix->frame_len, &hdr, &payload, &payload_len);
            FUZZ_ASSERT(err != dashcue_v1_ErrorCode_EC_NONE, "Corrupted header bit must be rejected");

            if (err == dashcue_v1_ErrorCode_EC_CRC) {
                header_crc_mismatches++;
            } else if (b < 4 && (err == dashcue_v1_ErrorCode_EC_UNKNOWN_TYPE || err == dashcue_v1_ErrorCode_EC_VERSION_MISMATCH)) {
                // Header magic / version checked before CRC!
                header_premature_type_checks++;
            }
        }
    }

    if (header_premature_type_checks > 0) {
        RECORD_VULNERABILITY("VULN-PR-01", "Parser Trust Inversion",
            "frame_decode inspects hdr.magic and hdr.version before validating CRC32 trailer, returning EC_UNKNOWN_TYPE / EC_VERSION_MISMATCH for corrupted frames instead of EC_CRC.");
    }
    printf("  Header bit-flips tested: 128, CRC rejected: %d, Premature magic/version rejected: %d\n",
           header_crc_mismatches, header_premature_type_checks);

    // 2. Bit flips across entire payload
    int payload_crc_rejections = 0;
    for (size_t p = 0; p < fix->pb_len; ++p) {
        for (int bit = 0; bit < 8; ++bit) {
            memcpy(corrupt_buf, fix->frame_bytes, fix->frame_len);
            corrupt_buf[DASHCUE_HEADER_SIZE + p] ^= (1u << bit);
            err = frame_decode(corrupt_buf, fix->frame_len, &hdr, &payload, &payload_len);
            FUZZ_ASSERT(err == dashcue_v1_ErrorCode_EC_CRC, "Payload bit corruption must yield EC_CRC");
            if (err == dashcue_v1_ErrorCode_EC_CRC) payload_crc_rejections++;
        }
    }
    printf("  Payload bit-flips tested: %u, CRC rejected: %d\n", (unsigned int)(fix->pb_len * 8), payload_crc_rejections);

    // 3. Bit flips across CRC trailer bytes
    int trailer_crc_rejections = 0;
    for (size_t t = 0; t < DASHCUE_TRAILER_SIZE; ++t) {
        for (int bit = 0; bit < 8; ++bit) {
            memcpy(corrupt_buf, fix->frame_bytes, fix->frame_len);
            corrupt_buf[DASHCUE_HEADER_SIZE + fix->pb_len + t] ^= (1u << bit);
            err = frame_decode(corrupt_buf, fix->frame_len, &hdr, &payload, &payload_len);
            FUZZ_ASSERT(err == dashcue_v1_ErrorCode_EC_CRC, "CRC trailer bit flip must yield EC_CRC");
            if (err == dashcue_v1_ErrorCode_EC_CRC) trailer_crc_rejections++;
        }
    }
    printf("  CRC trailer bit-flips tested: 32, CRC rejected: %d\n", trailer_crc_rejections);
}

// ---------------------------------------------------------------------------
// Suite 2: Payload Length Boundary Fuzzing
// ---------------------------------------------------------------------------
void fuzz_payload_length_bounds(void) {
    printf(ANSI_CYAN "\n=== [SUITE 2] Payload Length Boundary Fuzzing ===" ANSI_RESET "\n");

    FrameHeader hdr;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    // Test 1: Payload len = 0 (valid minimal frame: 16 bytes header + 0 payload + 4 CRC = 20 bytes)
    {
        uint8_t zero_payload_frame[20];
        FrameHeader zh;
        zh.magic = DASHCUE_FRAME_MAGIC;
        zh.version = DASHCUE_FRAME_VERSION;
        zh.flags = 0;
        zh.msg_type = dashcue_v1_MessageType_MT_HEARTBEAT;
        zh.msg_id = 42;
        zh.length = 0;
        zh.reserved = 0;

        size_t encoded_len = 0;
        bool enc_ok = frame_encode(&zh, NULL, 0, zero_payload_frame, &encoded_len, sizeof(zero_payload_frame));
        FUZZ_ASSERT(enc_ok, "frame_encode must succeed for 0-byte payload");
        FUZZ_ASSERT(encoded_len == 20, "0-byte payload frame must be exactly 20 bytes");

        dashcue_v1_ErrorCode err = frame_decode(zero_payload_frame, encoded_len, &hdr, &payload, &payload_len);
        FUZZ_ASSERT(err == dashcue_v1_ErrorCode_EC_NONE, "0-byte payload frame must decode cleanly with EC_NONE");
        FUZZ_ASSERT(payload_len == 0, "payload_len must be 0");
        FUZZ_ASSERT(payload == NULL, "payload ptr should be NULL when length is 0");
    }

    // Test 2: Payload len = 1 (boundary minimum positive)
    {
        uint8_t one_byte = 0xAA;
        uint8_t one_frame[21];
        FrameHeader oh;
        oh.magic = DASHCUE_FRAME_MAGIC;
        oh.version = DASHCUE_FRAME_VERSION;
        oh.flags = 0;
        oh.msg_type = dashcue_v1_MessageType_MT_HEARTBEAT;
        oh.msg_id = 43;
        oh.length = 1;
        oh.reserved = 0;

        size_t encoded_len = 0;
        bool enc_ok = frame_encode(&oh, &one_byte, 1, one_frame, &encoded_len, sizeof(one_frame));
        FUZZ_ASSERT(enc_ok, "frame_encode must succeed for 1-byte payload");
        dashcue_v1_ErrorCode err = frame_decode(one_frame, encoded_len, &hdr, &payload, &payload_len);
        FUZZ_ASSERT(err == dashcue_v1_ErrorCode_EC_NONE, "1-byte payload frame must decode with EC_NONE");
        FUZZ_ASSERT(payload_len == 1 && payload != NULL && *payload == 0xAA, "1-byte payload content verified");
    }

    // Test 3: Payload len = DASHCUE_MAX_PAYLOAD_SIZE (exact bound)
    {
        uint8_t *max_payload = (uint8_t *)malloc(DASHCUE_MAX_PAYLOAD_SIZE);
        assert(max_payload);
        memset(max_payload, 0x55, DASHCUE_MAX_PAYLOAD_SIZE);

        uint8_t *max_frame = (uint8_t *)malloc(DASHCUE_MAX_FRAME_SIZE + 64);
        assert(max_frame);

        FrameHeader mh;
        mh.magic = DASHCUE_FRAME_MAGIC;
        mh.version = DASHCUE_FRAME_VERSION;
        mh.flags = 0;
        mh.msg_type = dashcue_v1_MessageType_MT_ARTWORK_CHUNK;
        mh.msg_id = 44;
        mh.length = DASHCUE_MAX_PAYLOAD_SIZE;
        mh.reserved = 0;

        size_t encoded_len = 0;
        bool enc_ok = frame_encode(&mh, max_payload, DASHCUE_MAX_PAYLOAD_SIZE, max_frame, &encoded_len, DASHCUE_MAX_FRAME_SIZE + 64);
        FUZZ_ASSERT(enc_ok, "frame_encode must succeed for exact max payload");
        FUZZ_ASSERT(encoded_len == 16 + DASHCUE_MAX_PAYLOAD_SIZE + 4, "Encoded length must equal header + max_payload + trailer");

        dashcue_v1_ErrorCode err = frame_decode(max_frame, encoded_len, &hdr, &payload, &payload_len);
        FUZZ_ASSERT(err == dashcue_v1_ErrorCode_EC_NONE, "max payload must decode cleanly");
        FUZZ_ASSERT(payload_len == DASHCUE_MAX_PAYLOAD_SIZE, "Decoded payload_len must match max");

        free(max_payload);
        free(max_frame);
    }

    // Test 4: Payload len = DASHCUE_MAX_PAYLOAD_SIZE + 1 (boundary overflow by 1 byte)
    {
        uint8_t dummy[10] = {0};
        uint8_t out_buf[100];
        size_t out_len = 0;
        FrameHeader oh;
        oh.magic = DASHCUE_FRAME_MAGIC;
        oh.version = DASHCUE_FRAME_VERSION;
        oh.flags = 0;
        oh.msg_type = dashcue_v1_MessageType_MT_ARTWORK_CHUNK;
        oh.msg_id = 45;
        oh.length = DASHCUE_MAX_PAYLOAD_SIZE + 1;
        oh.reserved = 0;

        bool enc_ok = frame_encode(&oh, dummy, DASHCUE_MAX_PAYLOAD_SIZE + 1, out_buf, &out_len, sizeof(out_buf));
        FUZZ_ASSERT(!enc_ok, "frame_encode must reject oversized payload");

        // Manually build overlong header
        uint8_t raw_frame[20] = {0};
        raw_frame[0] = 0x5C; raw_frame[1] = 0xD4; // Magic
        raw_frame[2] = 0x01; // Version
        uint32_t bad_len = DASHCUE_MAX_PAYLOAD_SIZE + 1;
        raw_frame[10] = (uint8_t)(bad_len & 0xFF);
        raw_frame[11] = (uint8_t)((bad_len >> 8) & 0xFF);
        raw_frame[12] = (uint8_t)((bad_len >> 16) & 0xFF);
        raw_frame[13] = (uint8_t)((bad_len >> 24) & 0xFF);
        dashcue_v1_ErrorCode err = frame_decode(raw_frame, 20, &hdr, &payload, &payload_len);
        FUZZ_ASSERT(err == dashcue_v1_ErrorCode_EC_TOO_LARGE, "frame_decode must reject declared length over max as EC_TOO_LARGE");
    }

    // Test 5: Payload len = 65535 and 0xFFFFFFFF
    {
        uint8_t raw_frame[20] = {0};
        raw_frame[0] = 0x5C; raw_frame[1] = 0xD4;
        raw_frame[2] = 0x01;

        // 65535 (0x0000FFFF)
        raw_frame[10] = 0xFF; raw_frame[11] = 0xFF; raw_frame[12] = 0x00; raw_frame[13] = 0x00;
        dashcue_v1_ErrorCode err = frame_decode(raw_frame, 20, &hdr, &payload, &payload_len);
        FUZZ_ASSERT(err == dashcue_v1_ErrorCode_EC_TOO_LARGE, "Declared length 65535 must reject as EC_TOO_LARGE");

        // 0xFFFFFFFF
        raw_frame[10] = 0xFF; raw_frame[11] = 0xFF; raw_frame[12] = 0xFF; raw_frame[13] = 0xFF;
        err = frame_decode(raw_frame, 20, &hdr, &payload, &payload_len);
        FUZZ_ASSERT(err == dashcue_v1_ErrorCode_EC_TOO_LARGE, "Declared length 0xFFFFFFFF must reject as EC_TOO_LARGE");
    }

    // Test 6: Frame truncated bounds (0, 1, 15, 19 bytes)
    {
        uint8_t buf[20] = {0};
        dashcue_v1_ErrorCode err0 = frame_decode(buf, 0, &hdr, &payload, &payload_len);
        dashcue_v1_ErrorCode err1 = frame_decode(buf, 1, &hdr, &payload, &payload_len);
        dashcue_v1_ErrorCode err15 = frame_decode(buf, 15, &hdr, &payload, &payload_len);
        dashcue_v1_ErrorCode err19 = frame_decode(buf, 19, &hdr, &payload, &payload_len);

        FUZZ_ASSERT(err0 != dashcue_v1_ErrorCode_EC_NONE, "0-byte frame must be rejected");
        FUZZ_ASSERT(err1 != dashcue_v1_ErrorCode_EC_NONE, "1-byte frame must be rejected");
        FUZZ_ASSERT(err15 != dashcue_v1_ErrorCode_EC_NONE, "15-byte frame must be rejected");
        FUZZ_ASSERT(err19 != dashcue_v1_ErrorCode_EC_NONE, "19-byte frame must be rejected");

        if (err15 == dashcue_v1_ErrorCode_EC_TOO_LARGE) {
            RECORD_VULNERABILITY("VULN-PR-02", "Truncated Frame Semantic Mismatch",
                "frame_decode returns EC_TOO_LARGE for frames shorter than 20 bytes (e.g. 15 bytes) instead of EC_DECODE.");
        }
    }
}

// ---------------------------------------------------------------------------
// Suite 3: Fragment Index & Total Count Boundary Fuzzing
// ---------------------------------------------------------------------------
void fuzz_fragment_bounds(void) {
    printf(ANSI_CYAN "\n=== [SUITE 3] Fragment Index & Total Count Bounds ===" ANSI_RESET "\n");

    ReassemblyEngine engine;
    reassembly_init(&engine);

    const uint8_t *out = NULL;
    size_t out_len = 0;

    // Test 1: total_frags = 0
    uint8_t frag_zero_total[8] = {DASHCUE_LANE_CONTROL, 0, 0, 8, 0xAA, 0xBB, 0xCC, 0xDD};
    ReassemblyStatus st = reassembly_feed(&engine, frag_zero_total, sizeof(frag_zero_total), 1000, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_ERROR, "total_frags = 0 must be rejected with REASSEMBLY_ERROR");

    // Test 2: frag_idx >= total_frags (frag_idx = 1, total_frags = 1)
    uint8_t frag_idx_oob[8] = {DASHCUE_LANE_CONTROL, 1, 1, 8, 0x11, 0x22, 0x33, 0x44};
    st = reassembly_feed(&engine, frag_idx_oob, sizeof(frag_idx_oob), 1001, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_ERROR, "frag_idx >= total_frags must be rejected with REASSEMBLY_ERROR");

    // Test 3: frag_idx = 5, total_frags = 3
    uint8_t frag_idx_high[8] = {DASHCUE_LANE_BULK, 3, 5, 8, 0x55, 0x66, 0x77, 0x88};
    st = reassembly_feed(&engine, frag_idx_high, sizeof(frag_idx_high), 1002, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_ERROR, "frag_idx = 5 when total = 3 must return REASSEMBLY_ERROR");

    // Test 4: Fragment 0 missing (incoming stream begins at frag_idx = 1)
    uint8_t frag_orphan[8] = {DASHCUE_LANE_CONTROL, 4, 1, 8, 0x01, 0x02, 0x03, 0x04};
    st = reassembly_feed(&engine, frag_orphan, sizeof(frag_orphan), 1003, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_ERROR, "Stream starting with frag_idx = 1 without prior frag 0 must return REASSEMBLY_ERROR");
    FUZZ_ASSERT(engine.out_of_order_count == 1, "out_of_order_count must increment on missing frag 0");

    // Test 5: total_frags = 255 with large chunks (Pre-bounds check attack)
    reassembly_init(&engine);
    uint8_t huge_frag[244]; // 4-byte header + 240 byte chunk
    huge_frag[0] = DASHCUE_LANE_BULK;
    huge_frag[1] = 255; // 255 * 240 = 61,200 bytes >> 4096 max frame!
    huge_frag[2] = 0;   // frag 0
    huge_frag[3] = 8;
    memset(&huge_frag[4], 0x77, 240);

    st = reassembly_feed(&engine, huge_frag, sizeof(huge_frag), 2000, &out, &out_len);
    // Did it accept frag 0 or reject pre-bounds?
    if (st == REASSEMBLY_IN_PROGRESS && engine.lanes[DASHCUE_LANE_BULK].active) {
        RECORD_VULNERABILITY("VULN-PR-03", "Missing Pre-Allocation Bounds Check",
            "Firmware accepts frag 0 claiming total_frags = 255 * 240 bytes (61.2 KB), keeping lane active until buffer exhaustion at fragment 18 instead of rejecting upfront.");
    }
    FUZZ_ASSERT(engine.lanes[DASHCUE_LANE_BULK].active == true || st == REASSEMBLY_ERROR, "Tracked pre-bounds behavior");

    // Feed remaining fragments until buffer bounds trigger length_violation_count
    for (uint8_t idx = 1; idx < 20; ++idx) {
        huge_frag[2] = idx;
        st = reassembly_feed(&engine, huge_frag, sizeof(huge_frag), 2000 + idx * 10, &out, &out_len);
        if (st == REASSEMBLY_ERROR) {
            break;
        }
    }
    FUZZ_ASSERT(engine.length_violation_count == 1, "length_violation_count must increment when 4116-byte limit exceeded");
    FUZZ_ASSERT(engine.lanes[DASHCUE_LANE_BULK].active == false, "Lane must be reset after length violation");
}

// ---------------------------------------------------------------------------
// Suite 4: MTU 23 (Single-Byte Chunks) and MTU 247
// ---------------------------------------------------------------------------
void fuzz_mtu_boundaries_and_single_byte_chunks(void) {
    printf(ANSI_CYAN "\n=== [SUITE 4] MTU Boundaries & Single-Byte Chunk Streams ===" ANSI_RESET "\n");

    ReassemblyEngine engine;
    reassembly_init(&engine);

    // Test 1: Single-byte fragment payload stream (Stress test chunk_len = 1)
    // Construct a minimal valid 20-byte framed message
    uint8_t raw_msg[20];
    FrameHeader zh;
    zh.magic = DASHCUE_FRAME_MAGIC;
    zh.version = DASHCUE_FRAME_VERSION;
    zh.flags = 0;
    zh.msg_type = dashcue_v1_MessageType_MT_HEARTBEAT;
    zh.msg_id = 777;
    zh.length = 0;
    zh.reserved = 0;

    size_t framed_len = 0;
    frame_encode(&zh, NULL, 0, raw_msg, &framed_len, sizeof(raw_msg));
    FUZZ_ASSERT(framed_len == 20, "Framed len is 20 bytes");

    // Feed 20 fragments, each carrying exactly 1 byte of payload (frag_len = 5)
    const uint8_t *out = NULL;
    size_t out_len = 0;

    for (uint8_t i = 0; i < 20; ++i) {
        uint8_t frag[5];
        frag[0] = DASHCUE_LANE_CONTROL;
        frag[1] = 20; // 20 fragments total
        frag[2] = i;  // index 0..19
        frag[3] = 8;
        frag[4] = raw_msg[i]; // 1 byte chunk

        ReassemblyStatus st = reassembly_feed(&engine, frag, 5, 3000 + i * 5, &out, &out_len);
        if (i == 19) {
            FUZZ_ASSERT(st == REASSEMBLY_COMPLETE, "Single-byte payload stream must reassemble successfully");
            FUZZ_ASSERT(out_len == 20, "Reassembled size must equal 20 bytes");
            FUZZ_ASSERT(memcmp(out, raw_msg, 20) == 0, "Reassembled content matches byte-for-byte");
        } else {
            FUZZ_ASSERT(st == REASSEMBLY_IN_PROGRESS, "Intermediary single-byte frag is IN_PROGRESS");
        }
    }

    // Verify unframe of reassembled single-byte stream
    FrameHeader dec_hdr;
    const uint8_t *dec_payload = NULL;
    size_t dec_payload_len = 0;
    dashcue_v1_ErrorCode err = frame_decode(out, out_len, &dec_hdr, &dec_payload, &dec_payload_len);
    FUZZ_ASSERT(err == dashcue_v1_ErrorCode_EC_NONE, "Reassembled single-byte stream frame decodes with EC_NONE");
    FUZZ_ASSERT(dec_hdr.msg_id == 777, "Decoded msg_id matches 777");
}

// ---------------------------------------------------------------------------
// Suite 5: Out-of-Order Arrival & Duplicate Fragment Delivery
// ---------------------------------------------------------------------------
void fuzz_out_of_order_and_duplicates(void) {
    printf(ANSI_CYAN "\n=== [SUITE 5] Out-of-Order & Duplicate Fragments ===" ANSI_RESET "\n");

    ReassemblyEngine engine;
    reassembly_init(&engine);
    const uint8_t *out = NULL;
    size_t out_len = 0;

    // Pattern 1: Clean frame restart on frag 0 (R2) specifically for BULK lane
    uint8_t frag0_bulk[10] = {DASHCUE_LANE_BULK, 3, 0, 8, 1, 2, 3, 4, 5, 6};
    ReassemblyStatus st = reassembly_feed(&engine, frag0_bulk, sizeof(frag0_bulk), 4000, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_IN_PROGRESS, "Initial Bulk Frag 0 accepted");

    st = reassembly_feed(&engine, frag0_bulk, sizeof(frag0_bulk), 4010, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_IN_PROGRESS, "Clean frame restart on frag 0 returns REASSEMBLY_IN_PROGRESS");
    FUZZ_ASSERT(engine.out_of_order_count == 0, "out_of_order_count not incremented on clean restart");
    FUZZ_ASSERT(engine.lanes[DASHCUE_LANE_BULK].active, "Lane remains active on clean restart");
    FUZZ_ASSERT(engine.lanes[DASHCUE_LANE_BULK].next_frag_idx == 1, "Next frag idx reset to 1");

    // Pattern 1b: Duplicate frag 0 on CONTROL lane strictly rejected
    uint8_t frag0[10] = {DASHCUE_LANE_CONTROL, 3, 0, 8, 1, 2, 3, 4, 5, 6};
    st = reassembly_feed(&engine, frag0, sizeof(frag0), 4020, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_IN_PROGRESS, "Initial Control Frag 0 accepted");
    st = reassembly_feed(&engine, frag0, sizeof(frag0), 4030, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_ERROR, "Duplicate Frag 0 on CONTROL lane must return REASSEMBLY_ERROR");
    FUZZ_ASSERT(engine.out_of_order_count == 1, "out_of_order_count incremented on duplicate control frag 0");
    FUZZ_ASSERT(!engine.lanes[DASHCUE_LANE_CONTROL].active, "Control lane deactivated on duplicate frag 0");

    // Pattern 2: Skipping an index (0, then 2)
    reassembly_init(&engine);
    uint8_t frag2[10] = {DASHCUE_LANE_CONTROL, 3, 2, 8, 7, 8, 9, 0, 1, 2};
    st = reassembly_feed(&engine, frag0, sizeof(frag0), 4100, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_IN_PROGRESS, "Frag 0 accepted");

    st = reassembly_feed(&engine, frag2, sizeof(frag2), 4110, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_ERROR, "Skipped frag 1 must return REASSEMBLY_ERROR");
    FUZZ_ASSERT(engine.out_of_order_count == 1, "out_of_order_count incremented on skipped index");

    // Pattern 3: Duplicate intermediate fragment (0, 1, 1)
    reassembly_init(&engine);
    uint8_t frag1[10] = {DASHCUE_LANE_CONTROL, 3, 1, 8, 3, 4, 5, 6, 7, 8};
    reassembly_feed(&engine, frag0, sizeof(frag0), 4200, &out, &out_len);
    st = reassembly_feed(&engine, frag1, sizeof(frag1), 4210, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_IN_PROGRESS, "Frag 1 accepted");

    st = reassembly_feed(&engine, frag1, sizeof(frag1), 4220, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_ERROR, "Duplicate Frag 1 must return REASSEMBLY_ERROR");
    FUZZ_ASSERT(engine.out_of_order_count == 1, "out_of_order_count incremented on duplicate frag 1");
}

// ---------------------------------------------------------------------------
// Suite 6: Interleaved Multi-Lane Streaming
// ---------------------------------------------------------------------------
void fuzz_interleaved_multi_lane(void) {
    printf(ANSI_CYAN "\n=== [SUITE 6] Interleaved Multi-Lane Streaming ===" ANSI_RESET "\n");

    ReassemblyEngine engine;
    reassembly_init(&engine);
    const uint8_t *out = NULL;
    size_t out_len = 0;

    // Start 10-fragment Bulk transfer on Lane 1
    for (uint8_t b = 0; b < 5; ++b) {
        uint8_t bulk_frag[14];
        bulk_frag[0] = DASHCUE_LANE_BULK;
        bulk_frag[1] = 10;
        bulk_frag[2] = b;
        bulk_frag[3] = 8;
        memset(&bulk_frag[4], 0xB0 + b, 10);
        ReassemblyStatus st = reassembly_feed(&engine, bulk_frag, sizeof(bulk_frag), 5000 + b * 10, &out, &out_len);
        FUZZ_ASSERT(st == REASSEMBLY_IN_PROGRESS, "Bulk frag accepted");
    }

    // Interleave High-Priority Control message on Lane 0 (2 fragments)
    uint8_t c0[14] = {DASHCUE_LANE_CONTROL, 2, 0, 8, 'C', 'M', 'D', '0', 0, 0, 0, 0, 0, 0};
    uint8_t c1[14] = {DASHCUE_LANE_CONTROL, 2, 1, 8, 'C', 'M', 'D', '1', 1, 1, 1, 1, 1, 1};

    ReassemblyStatus st = reassembly_feed(&engine, c0, sizeof(c0), 5055, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_IN_PROGRESS, "Lane 0 frag 0 in progress");

    st = reassembly_feed(&engine, c1, sizeof(c1), 5060, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_COMPLETE, "Lane 0 completes immediately during active bulk lane");
    FUZZ_ASSERT(out_len == 20, "Lane 0 message length is 20 bytes");

    // Resume Bulk transfer on Lane 1 (frags 5..9)
    for (uint8_t b = 5; b < 10; ++b) {
        uint8_t bulk_frag[14];
        bulk_frag[0] = DASHCUE_LANE_BULK;
        bulk_frag[1] = 10;
        bulk_frag[2] = b;
        bulk_frag[3] = 8;
        memset(&bulk_frag[4], 0xB0 + b, 10);
        st = reassembly_feed(&engine, bulk_frag, sizeof(bulk_frag), 5100 + b * 10, &out, &out_len);
        if (b == 9) {
            FUZZ_ASSERT(st == REASSEMBLY_COMPLETE, "Bulk lane completes cleanly after Lane 0 interruption");
            FUZZ_ASSERT(out_len == 100, "Bulk payload total is 100 bytes");
        } else {
            FUZZ_ASSERT(st == REASSEMBLY_IN_PROGRESS, "Bulk remaining frag in progress");
        }
    }
}

// ---------------------------------------------------------------------------
// Suite 7: Reassembly Timeout Fuzzing (2.0s Gap Bound)
// ---------------------------------------------------------------------------
void fuzz_reassembly_timeout(void) {
    printf(ANSI_CYAN "\n=== [SUITE 7] Reassembly Gap Timeout (2.0s Bounds) ===" ANSI_RESET "\n");

    ReassemblyEngine engine;
    reassembly_init(&engine);
    const uint8_t *out = NULL;
    size_t out_len = 0;

    uint8_t f0[10] = {DASHCUE_LANE_BULK, 3, 0, 8, 1, 2, 3, 4, 5, 6};
    uint8_t f1[10] = {DASHCUE_LANE_BULK, 3, 1, 8, 7, 8, 9, 0, 1, 2};

    // Subtest A: Gap of 1999 ms (< 2000 ms) -> SUCCESS
    reassembly_feed(&engine, f0, sizeof(f0), 1000, &out, &out_len);
    ReassemblyStatus st = reassembly_feed(&engine, f1, sizeof(f1), 2999, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_IN_PROGRESS, "1999 ms gap within 2.0s threshold must NOT time out");
    FUZZ_ASSERT(engine.frag_timeouts_count == 0, "No timeouts counted at 1999 ms");

    // Subtest B: Gap of 2001 ms (> 2000 ms) -> TIMEOUT
    reassembly_init(&engine);
    reassembly_feed(&engine, f0, sizeof(f0), 1000, &out, &out_len);
    st = reassembly_feed(&engine, f1, sizeof(f1), 3001, &out, &out_len);
    FUZZ_ASSERT(st == REASSEMBLY_TIMEOUT, "2001 ms gap must return REASSEMBLY_TIMEOUT");
    FUZZ_ASSERT(engine.frag_timeouts_count == 1, "frag_timeouts_count incremented to 1");
    FUZZ_ASSERT(!engine.lanes[DASHCUE_LANE_BULK].active, "Lane reset after 2001 ms timeout");

    // Subtest C: Periodic tick expiration
    reassembly_init(&engine);
    reassembly_feed(&engine, f0, sizeof(f0), 5000, &out, &out_len);
    reassembly_tick(&engine, 7001); // 2001 ms later via background tick
    FUZZ_ASSERT(engine.frag_timeouts_count == 1, "Background tick expired dead lane");
    FUZZ_ASSERT(!engine.lanes[DASHCUE_LANE_BULK].active, "Lane inactive after background tick");
}

// ---------------------------------------------------------------------------
// Suite 8: Credit Flow Control Stress & Starvation
// ---------------------------------------------------------------------------
void fuzz_flow_control_credit_bounds(void) {
    printf(ANSI_CYAN "\n=== [SUITE 8] Credit Flow Control Stress & Starvation ===" ANSI_RESET "\n");

    CreditFlowController fc;
    flow_control_init(&fc, 8);
    FUZZ_ASSERT(flow_control_get_credits(&fc) == 8, "Initial credits must be 8");

    // 1. Bulk lane consumes exactly 8 credits
    for (int i = 0; i < 8; ++i) {
        FUZZ_ASSERT(flow_control_can_send(&fc, DASHCUE_LANE_BULK), "Can send bulk within credit window");
        FUZZ_ASSERT(flow_control_consume(&fc, DASHCUE_LANE_BULK), "Consume bulk credit succeeds");
    }
    FUZZ_ASSERT(flow_control_get_credits(&fc) == 0, "Credits depleted to 0");

    // 2. 9th bulk transmission blocked
    FUZZ_ASSERT(!flow_control_can_send(&fc, DASHCUE_LANE_BULK), "9th bulk transmission blocked at 0 credits");
    FUZZ_ASSERT(!flow_control_consume(&fc, DASHCUE_LANE_BULK), "Consuming at 0 credits fails");

    // 3. Control lane behavior when credits == 0
    bool ctrl_can_send = flow_control_can_send(&fc, DASHCUE_LANE_CONTROL);
    bool ctrl_consumed = flow_control_consume(&fc, DASHCUE_LANE_CONTROL);
    FUZZ_ASSERT(ctrl_can_send && ctrl_consumed, "Control lane can send when credits = 0 (Lane 0 reservation)");

    // The reserved control path intentionally stays available at zero credits.
    // This unit test cannot establish receiver throughput or denial-of-service resistance.
    for (int i = 0; i < 100; ++i) {
        flow_control_consume(&fc, DASHCUE_LANE_CONTROL);
    }
    FUZZ_ASSERT(flow_control_get_credits(&fc) == 0, "Reserved control path must not underflow credits");
    printf("  Observation: reserved control path remains available at zero credits; hardware flood stability NOT TESTED.\n");

    // 4. Credit grant clamping (grant 20 credits, verify clamped to 8)
    flow_control_grant(&fc, 20);
    FUZZ_ASSERT(flow_control_get_credits(&fc) == 8, "Credits clamped to DASHCUE_INITIAL_CREDITS (8)");
    FUZZ_ASSERT(flow_control_consume(&fc, DASHCUE_LANE_CONTROL), "Control send succeeds with available credit");
    FUZZ_ASSERT(flow_control_get_credits(&fc) == 7, "Control sends consume available credits");
}

// ---------------------------------------------------------------------------
// Suite 9: Deduplication Cache Stress & TTL Expiry
// ---------------------------------------------------------------------------
void fuzz_dedup_cache_boundaries(void) {
    printf(ANSI_CYAN "\n=== [SUITE 9] Deduplication Cache Stress (32-entry & 60s TTL) ===" ANSI_RESET "\n");

    DedupCache cache;
    dedup_init(&cache);
    dashcue_v1_CmdResult res;

    // 1. Fill all 32 slots with unique commands at t = 1000..1031 ms
    for (uint32_t cid = 1; cid <= 32; ++cid) {
        bool dup = dedup_check_and_record(&cache, cid, 1000 + cid, &res);
        FUZZ_ASSERT(!dup, "Fresh command must not be flagged duplicate");
        dedup_update_result(&cache, cid, dashcue_v1_CmdResult_Status_ST_CONFIRMED, dashcue_v1_ErrorCode_EC_NONE, 1000 + cid + 10);
    }
    FUZZ_ASSERT(dedup_get_duplicate_count(&cache) == 0, "Zero duplicates on initial insertion");

    // 2. Verify all 32 commands hit as duplicates at t = 2000 ms
    for (uint32_t cid = 1; cid <= 32; ++cid) {
        bool dup = dedup_check_and_record(&cache, cid, 2000, &res);
        FUZZ_ASSERT(dup, "Existing command within TTL must be duplicate");
        FUZZ_ASSERT(res.command_id == cid, "Replayed command_id matches");
        FUZZ_ASSERT(res.status == dashcue_v1_CmdResult_Status_ST_CONFIRMED, "Replayed status matches CONFIRMED");
    }
    FUZZ_ASSERT(dedup_get_duplicate_count(&cache) == 32, "All 32 duplicates counted");

    // 3. Overflow: Insert 33rd command at t = 3000 ms -> should evict command 1 (oldest)
    bool dup33 = dedup_check_and_record(&cache, 33, 3000, &res);
    FUZZ_ASSERT(!dup33, "Command 33 is fresh");

    // Verify command 2 is still in cache (duplicate check does not allocate or evict)
    bool dup2 = dedup_check_and_record(&cache, 2, 3010, &res);
    FUZZ_ASSERT(dup2, "Command 2 remains in cache");

    // Verify command 1 was evicted by command 33 (not duplicate; will re-allocate slot and evict next oldest)
    bool dup1 = dedup_check_and_record(&cache, 1, 3020, &res);
    FUZZ_ASSERT(!dup1, "Command 1 was evicted by command 33 and is treated as fresh");

    // 4. TTL Expiry boundary: command 2 recorded at t = 1002 ms.
    // At t = 61002 ms (gap = 60,000 ms = exactly 60.0s) -> DUPLICATE
    // At t = 61003 ms (gap = 60,001 ms = 60.001s > 60.0s TTL) -> EXPIRED / NEW COMMAND
    dedup_init(&cache);
    dedup_check_and_record(&cache, 500, 1000, &res);
    dedup_update_result(&cache, 500, dashcue_v1_CmdResult_Status_ST_CONFIRMED, dashcue_v1_ErrorCode_EC_NONE, 1100);

    // Test at 60,000 ms (exactly on 60.0s TTL boundary)
    bool dup_boundary = dedup_check_and_record(&cache, 500, 61000, &res);
    FUZZ_ASSERT(dup_boundary, "Arrival at exact 60.0 s (gap 60,000 ms) treated as duplicate");

    // Test at 60,001 ms (> 60.0s TTL boundary)
    dedup_init(&cache);
    dedup_check_and_record(&cache, 501, 1000, &res);
    bool dup_expired = dedup_check_and_record(&cache, 501, 61001, &res);
    FUZZ_ASSERT(!dup_expired, "Arrival at 60.001 s (gap 60,001 ms) treated as EXPIRED / NEW");
}

// ---------------------------------------------------------------------------
// Suite 10: Seeded random bytes against the production frame/reassembly parsers.
// ---------------------------------------------------------------------------
void fuzz_randomized_mutations(void) {
    printf(ANSI_CYAN "\n=== [SUITE 10] Seeded Random Inputs (%u iterations, seed 0xC001CAFE) ===" ANSI_RESET "\n", g_fuzz_iterations);

    ReassemblyEngine engine;
    reassembly_init(&engine);

    srand(0xC001CAFE);
    uint8_t buffer[600];
    FrameHeader hdr;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    const uint8_t *out = NULL;
    size_t out_len = 0;

    int decode_rejections = 0;
    int reassembly_rejections = 0;

    for (unsigned int i = 0; i < g_fuzz_iterations; ++i) {
        size_t len = (size_t)(rand() % sizeof(buffer));
        for (size_t j = 0; j < len; ++j) {
            buffer[j] = (uint8_t)(rand() & 0xFF);
        }

        // Fuzz frame_decode
        dashcue_v1_ErrorCode err = frame_decode(buffer, len, &hdr, &payload, &payload_len);
        if (err != dashcue_v1_ErrorCode_EC_NONE) {
            decode_rejections++;
        }

        // Fuzz reassembly_feed
        ReassemblyStatus st = reassembly_feed(&engine, buffer, len, 10000 + i, &out, &out_len);
        if (st == REASSEMBLY_ERROR || st == REASSEMBLY_TIMEOUT) {
            reassembly_rejections++;
        }

        if (i % 2000 == 0) {
            reassembly_tick(&engine, 10000 + i + 2500);
        }
    }

    printf("  Fuzzed %u inputs: decode_rejections = %d, reassembly_rejections = %d; process reached completion.\n",
           g_fuzz_iterations, decode_rejections, reassembly_rejections);
    FUZZ_ASSERT((unsigned int)decode_rejections > g_fuzz_iterations * 99 / 100, "Malformed random frames almost all rejected");
}

// ---------------------------------------------------------------------------
// Main Test Runner
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    if (argc > 2) return 2;
    if (argc == 2) {
        char *end = NULL;
        unsigned long count = strtoul(argv[1], &end, 10);
        if (!end || *end || count < 100 || count > 10000000) return 2;
        g_fuzz_iterations = (unsigned int)count;
    }
    printf(ANSI_CYAN "====================================================================\n");
    printf(" DashCue Adversarial Protocol Fuzz & Boundary Verification Harness\n");
    printf("====================================================================" ANSI_RESET "\n");

    fuzz_crc32_corruption();
    fuzz_payload_length_bounds();
    fuzz_fragment_bounds();
    fuzz_mtu_boundaries_and_single_byte_chunks();
    fuzz_out_of_order_and_duplicates();
    fuzz_interleaved_multi_lane();
    fuzz_reassembly_timeout();
    fuzz_flow_control_credit_bounds();
    fuzz_dedup_cache_boundaries();
    fuzz_randomized_mutations();

    printf("\n" ANSI_CYAN "====================================================================" ANSI_RESET "\n");
    printf(" RESULTS SUMMARY:\n");
    printf("   Total Assertions Run : %d\n", g_tests_run);
    printf("   Assertions Passed    : " ANSI_GREEN "%d" ANSI_RESET "\n", g_tests_passed);
    printf("   Assertions Failed    : " ANSI_RED "%d" ANSI_RESET "\n", g_tests_failed);
    printf("   Vulnerabilities Found: " ANSI_YELLOW "%d" ANSI_RESET "\n", g_vulnerabilities_found);
    printf(ANSI_CYAN "====================================================================" ANSI_RESET "\n\n");

    return (g_tests_failed == 0 && g_vulnerabilities_found == 0) ? 0 : 1;
}
