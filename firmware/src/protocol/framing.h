#ifndef DASHCUE_PROTOCOL_FRAMING_H
#define DASHCUE_PROTOCOL_FRAMING_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "generated/dashcue.pb.h"

#define DASHCUE_FRAME_MAGIC       0xD45Cu
#define DASHCUE_FRAME_VERSION     0x01u
#define DASHCUE_MAX_PAYLOAD_SIZE  4096u
#define DASHCUE_HEADER_SIZE       16u
#define DASHCUE_TRAILER_SIZE      4u
#define DASHCUE_MIN_FRAME_SIZE    (DASHCUE_HEADER_SIZE + DASHCUE_TRAILER_SIZE)
#define DASHCUE_MAX_FRAME_SIZE    (DASHCUE_HEADER_SIZE + DASHCUE_MAX_PAYLOAD_SIZE + DASHCUE_TRAILER_SIZE)

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
typedef struct {
    uint16_t magic;       // Must be DASHCUE_FRAME_MAGIC (0xD45C)
    uint8_t  version;     // Must be DASHCUE_FRAME_VERSION (0x01)
    uint8_t  flags;       // EnvelopeFlag bitmask (EF_ACK_REQUIRED, etc.)
    uint16_t msg_type;    // dashcue_v1_MessageType
    uint32_t msg_id;      // Sequence / correlation / command ID
    uint32_t length;      // Protobuf payload length (<= 4096)
    uint16_t reserved;    // Reserved / correlation context (default 0)
} FrameHeader;
#pragma pack(pop)

/**
 * Encode a message frame: 16-byte header + payload + 4-byte IEEE 802.3 CRC32 trailer.
 *
 * @param header Header metadata to encode.
 * @param payload Pointer to raw protobuf payload.
 * @param payload_len Length of payload in bytes (must be <= 4096).
 * @param out_frame Destination buffer for the enveloped frame.
 * @param out_frame_len Output: total encoded frame length (16 + payload_len + 4).
 * @param max_out_len Capacity of out_frame buffer.
 * @return true on success, false if parameters or buffer bounds are invalid.
 */
bool frame_encode(const FrameHeader *header,
                  const uint8_t *payload,
                  size_t payload_len,
                  uint8_t *out_frame,
                  size_t *out_frame_len,
                  size_t max_out_len);

/**
 * Decode and validate an incoming frame.
 * Checks bounds, magic, version, CRC32, and message type.
 *
 * @param frame_bytes Pointer to full framed message.
 * @param frame_len Total length of framed message in bytes.
 * @param out_header Output: parsed FrameHeader.
 * @param out_payload Output: pointer to beginning of protobuf payload inside frame_bytes.
 * @param out_payload_len Output: payload length in bytes.
 * @return EC_NONE (0) on success, or appropriate ErrorCode on rejection.
 */
dashcue_v1_ErrorCode frame_decode(const uint8_t *frame_bytes,
                                  size_t frame_len,
                                  FrameHeader *out_header,
                                  const uint8_t **out_payload,
                                  size_t *out_payload_len);

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_PROTOCOL_FRAMING_H
