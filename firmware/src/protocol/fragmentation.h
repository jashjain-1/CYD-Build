#ifndef DASHCUE_PROTOCOL_FRAGMENTATION_H
#define DASHCUE_PROTOCOL_FRAGMENTATION_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "framing.h"
#include "generated/dashcue.pb.h"

#define DASHCUE_FRAGMENT_HEADER_SIZE   4u
#define DASHCUE_NUM_LANES              2u
#define DASHCUE_LANE_CONTROL           0u
#define DASHCUE_LANE_BULK              1u
#define DASHCUE_REASSEMBLY_TIMEOUT_MS  2000u
#define DASHCUE_INITIAL_CREDITS        8u
#define DASHCUE_MAX_FRAGMENTS          255u

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
typedef struct {
    uint8_t lane;             // Stream ID / Lane (0 = Control, 1 = Bulk)
    uint8_t total_frags;      // Total number of fragments (1..255)
    uint8_t frag_idx;         // 0-based fragment index (0 .. total_frags - 1)
    uint8_t reserved_credits; // Flow control credit or reserved
} FragmentHeader;
#pragma pack(pop)

/**
 * Result of feeding a fragment into the ReassemblyEngine.
 */
typedef enum {
    REASSEMBLY_IN_PROGRESS = 0,
    REASSEMBLY_COMPLETE    = 1,
    REASSEMBLY_ERROR       = 2,
    REASSEMBLY_TIMEOUT     = 3
} ReassemblyStatus;

/**
 * Single reassembly lane state (zero-heap static allocation).
 */
typedef struct {
    bool active;
    uint8_t total_frags;
    uint8_t next_frag_idx;
    size_t received_bytes;
    uint64_t last_frag_time_ms;
    uint8_t buffer[DASHCUE_MAX_FRAME_SIZE];
} ReassemblyLane;

/**
 * Complete Reassembly Engine maintaining 2 concurrent lanes and error metrics.
 */
typedef struct {
    ReassemblyLane lanes[DASHCUE_NUM_LANES];
    uint32_t frag_timeouts_count;
    uint32_t out_of_order_count;
    uint32_t length_violation_count;
} ReassemblyEngine;

/**
 * Credit flow controller state.
 */
typedef struct {
    uint8_t available_credits;
} CreditFlowController;

// --- Engine API ---

/**
 * Initialize reassembly engine and clear lanes.
 */
void reassembly_init(ReassemblyEngine *engine);

/**
 * Feed a raw BLE fragment (4-byte header + chunk payload) into the engine.
 *
 * @param engine Pointer to ReassemblyEngine.
 * @param frag_data Pointer to fragment packet bytes.
 * @param frag_len Length of fragment packet in bytes.
 * @param now_ms Current monotonic millisecond timestamp.
 * @param out_msg_bytes Output: pointer to reassembled message buffer if COMPLETE.
 * @param out_msg_len Output: length of reassembled message if COMPLETE.
 * @return ReassemblyStatus (IN_PROGRESS, COMPLETE, ERROR, or TIMEOUT).
 */
ReassemblyStatus reassembly_feed(ReassemblyEngine *engine,
                                const uint8_t *frag_data,
                                size_t frag_len,
                                uint64_t now_ms,
                                const uint8_t **out_msg_bytes,
                                size_t *out_msg_len);

/**
 * Periodic tick to expire lanes exceeding the 2.0 s timeout.
 */
void reassembly_tick(ReassemblyEngine *engine, uint64_t now_ms);

/**
 * Reset a specific lane.
 */
void reassembly_reset_lane(ReassemblyEngine *engine, uint8_t lane);

// --- Fragmentation API ---

/**
 * Calculate the max chunk payload size for a given ATT MTU.
 * Chunk size = (MTU - 3) - 4 = MTU - 7.
 */
size_t fragment_calc_chunk_size(uint16_t mtu);

/**
 * Calculate the total fragment count required for a message given MTU.
 */
uint8_t fragment_calc_count(size_t msg_len, uint16_t mtu);

/**
 * Pack a single fragment given the original framed message, fragment index, and MTU.
 *
 * @param msg_bytes Pointer to full framed message.
 * @param msg_len Total length of framed message.
 * @param lane Lane index (0 or 1).
 * @param frag_idx 0-based fragment index to pack.
 * @param mtu Negotiated ATT MTU (e.g. 23, 247).
 * @param credits Flow control credit field value.
 * @param out_frag Buffer to receive 4-byte header + chunk payload.
 * @param out_frag_len Output: total fragment size (4 + chunk_len).
 * @param max_frag_len Capacity of out_frag buffer.
 * @return true on success, false on invalid parameters or bounds.
 */
bool fragment_pack(const uint8_t *msg_bytes,
                   size_t msg_len,
                   uint8_t lane,
                   uint8_t frag_idx,
                   uint16_t mtu,
                   uint8_t credits,
                   uint8_t *out_frag,
                   size_t *out_frag_len,
                   size_t max_frag_len);

// --- Credit Flow Control API ---

void flow_control_init(CreditFlowController *fc, uint8_t initial_credits);
bool flow_control_can_send(const CreditFlowController *fc, uint8_t lane);
bool flow_control_consume(CreditFlowController *fc, uint8_t lane);
void flow_control_grant(CreditFlowController *fc, uint8_t credits);
uint8_t flow_control_get_credits(const CreditFlowController *fc);

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_PROTOCOL_FRAGMENTATION_H
