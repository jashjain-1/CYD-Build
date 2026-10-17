#include "framing.h"
#include "crc32.h"
#include <string.h>

static inline uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void write_u16_le(uint8_t *p, uint16_t val) {
    p[0] = (uint8_t)(val & 0xFFu);
    p[1] = (uint8_t)((val >> 8) & 0xFFu);
}

static inline void write_u32_le(uint8_t *p, uint32_t val) {
    p[0] = (uint8_t)(val & 0xFFu);
    p[1] = (uint8_t)((val >> 8) & 0xFFu);
    p[2] = (uint8_t)((val >> 16) & 0xFFu);
    p[3] = (uint8_t)((val >> 24) & 0xFFu);
}

bool frame_encode(const FrameHeader *header,
                  const uint8_t *payload,
                  size_t payload_len,
                  uint8_t *out_frame,
                  size_t *out_frame_len,
                  size_t max_out_len) {
    if (!header || !out_frame || !out_frame_len) {
        return false;
    }
    if (payload_len > DASHCUE_MAX_PAYLOAD_SIZE) {
        return false;
    }
    const size_t total_len = DASHCUE_HEADER_SIZE + payload_len + DASHCUE_TRAILER_SIZE;
    if (total_len > max_out_len) {
        return false;
    }

    // Write 16-byte header
    write_u16_le(&out_frame[0], header->magic);
    out_frame[2] = header->version;
    out_frame[3] = header->flags;
    write_u16_le(&out_frame[4], header->msg_type);
    write_u32_le(&out_frame[6], header->msg_id);
    write_u32_le(&out_frame[10], (uint32_t)payload_len);
    write_u16_le(&out_frame[14], header->reserved);

    // Copy payload
    if (payload_len > 0) {
        if (!payload) {
            return false;
        }
        memcpy(&out_frame[DASHCUE_HEADER_SIZE], payload, payload_len);
    }

    // Calculate CRC32 over header + payload
    const uint32_t crc = crc32_calculate(out_frame, DASHCUE_HEADER_SIZE + payload_len);
    write_u32_le(&out_frame[DASHCUE_HEADER_SIZE + payload_len], crc);

    *out_frame_len = total_len;
    return true;
}

dashcue_v1_ErrorCode frame_decode(const uint8_t *frame_bytes,
                                  size_t frame_len,
                                  FrameHeader *out_header,
                                  const uint8_t **out_payload,
                                  size_t *out_payload_len) {
    if (!frame_bytes || !out_header) {
        return dashcue_v1_ErrorCode_EC_DECODE;
    }

    // 1. Length bounds (cheapest check)
    if (frame_len < DASHCUE_MIN_FRAME_SIZE) {
        return dashcue_v1_ErrorCode_EC_DECODE;
    }
    if (frame_len > DASHCUE_MAX_FRAME_SIZE) {
        return dashcue_v1_ErrorCode_EC_TOO_LARGE;
    }

    // Parse header fields safely
    FrameHeader hdr;
    hdr.magic = read_u16_le(&frame_bytes[0]);
    hdr.version = frame_bytes[2];
    hdr.flags = frame_bytes[3];
    hdr.msg_type = read_u16_le(&frame_bytes[4]);
    hdr.msg_id = read_u32_le(&frame_bytes[6]);
    hdr.length = read_u32_le(&frame_bytes[10]);
    hdr.reserved = read_u16_le(&frame_bytes[14]);

    // Check payload length against maximum permitted
    if (hdr.length > DASHCUE_MAX_PAYLOAD_SIZE) {
        return dashcue_v1_ErrorCode_EC_TOO_LARGE;
    }

    // Check that total frame length matches expected header + payload + trailer
    if (frame_len != DASHCUE_HEADER_SIZE + hdr.length + DASHCUE_TRAILER_SIZE) {
        return (frame_len < DASHCUE_HEADER_SIZE + hdr.length + DASHCUE_TRAILER_SIZE)
                   ? dashcue_v1_ErrorCode_EC_DECODE
                   : dashcue_v1_ErrorCode_EC_TOO_LARGE;
    }

    // 2. CRC32 check (VULN-PR-01: validate CRC before trusting header fields)
    const size_t content_len = DASHCUE_HEADER_SIZE + hdr.length;
    const uint32_t expected_crc = read_u32_le(&frame_bytes[content_len]);
    const uint32_t actual_crc = crc32_calculate(frame_bytes, content_len);
    if (expected_crc != actual_crc) {
        return dashcue_v1_ErrorCode_EC_CRC;
    }

    // 3. Magic check
    if (hdr.magic != DASHCUE_FRAME_MAGIC) {
        return dashcue_v1_ErrorCode_EC_UNKNOWN_TYPE;
    }

    // 4. Version check
    if (hdr.version != DASHCUE_FRAME_VERSION) {
        return dashcue_v1_ErrorCode_EC_VERSION_MISMATCH;
    }

    // Output parsed header and payload pointers
    *out_header = hdr;
    if (out_payload) {
        *out_payload = (hdr.length > 0) ? &frame_bytes[DASHCUE_HEADER_SIZE] : NULL;
    }
    if (out_payload_len) {
        *out_payload_len = hdr.length;
    }

    return dashcue_v1_ErrorCode_EC_NONE;
}
