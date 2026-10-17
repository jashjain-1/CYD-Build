#include "fragmentation.h"
#include <string.h>

size_t fragment_calc_chunk_size(uint16_t mtu) {
    if (mtu < 23) {
        mtu = 23;
    }
    // Effective ATT payload is MTU - 3.
    // 4-byte fragment header leaves MTU - 7 for chunk data.
    return (size_t)(mtu - 7);
}

uint8_t fragment_calc_count(size_t msg_len, uint16_t mtu) {
    const size_t chunk_size = fragment_calc_chunk_size(mtu);
    if (chunk_size == 0) {
        return 0;
    }
    const size_t count = (msg_len + chunk_size - 1) / chunk_size;
    if (count == 0) {
        return 1;
    }
    if (count > DASHCUE_MAX_FRAGMENTS) {
        return 0;
    }
    return (uint8_t)count;
}

bool fragment_pack(const uint8_t *msg_bytes,
                   size_t msg_len,
                   uint8_t lane,
                   uint8_t frag_idx,
                   uint16_t mtu,
                   uint8_t credits,
                   uint8_t *out_frag,
                   size_t *out_frag_len,
                   size_t max_frag_len) {
    if (!msg_bytes || !out_frag || !out_frag_len) {
        return false;
    }
    if (lane >= DASHCUE_NUM_LANES) {
        return false;
    }
    const size_t chunk_size = fragment_calc_chunk_size(mtu);
    const uint8_t total_frags = fragment_calc_count(msg_len, mtu);
    if (total_frags == 0 || frag_idx >= total_frags) {
        return false;
    }

    const size_t offset = (size_t)frag_idx * chunk_size;
    if (offset >= msg_len && msg_len > 0) {
        return false;
    }

    size_t chunk_len = msg_len - offset;
    if (chunk_len > chunk_size) {
        chunk_len = chunk_size;
    }

    const size_t total_frag_len = DASHCUE_FRAGMENT_HEADER_SIZE + chunk_len;
    if (total_frag_len > max_frag_len) {
        return false;
    }

    out_frag[0] = lane;
    out_frag[1] = total_frags;
    out_frag[2] = frag_idx;
    out_frag[3] = credits;

    if (chunk_len > 0) {
        memcpy(&out_frag[DASHCUE_FRAGMENT_HEADER_SIZE], &msg_bytes[offset], chunk_len);
    }

    *out_frag_len = total_frag_len;
    return true;
}

void reassembly_init(ReassemblyEngine *engine) {
    if (!engine) {
        return;
    }
    memset(engine, 0, sizeof(*engine));
}

void reassembly_reset_lane(ReassemblyEngine *engine, uint8_t lane) {
    if (!engine || lane >= DASHCUE_NUM_LANES) {
        return;
    }
    ReassemblyLane *l = &engine->lanes[lane];
    l->active = false;
    l->total_frags = 0;
    l->next_frag_idx = 0;
    l->received_bytes = 0;
    l->last_frag_time_ms = 0;
}

void reassembly_tick(ReassemblyEngine *engine, uint64_t now_ms) {
    if (!engine) {
        return;
    }
    for (uint8_t lane = 0; lane < DASHCUE_NUM_LANES; ++lane) {
        ReassemblyLane *l = &engine->lanes[lane];
        if (l->active && (now_ms - l->last_frag_time_ms >= DASHCUE_REASSEMBLY_TIMEOUT_MS)) {
            engine->frag_timeouts_count++;
            reassembly_reset_lane(engine, lane);
        }
    }
}

ReassemblyStatus reassembly_feed(ReassemblyEngine *engine,
                                const uint8_t *frag_data,
                                size_t frag_len,
                                uint64_t now_ms,
                                const uint8_t **out_msg_bytes,
                                size_t *out_msg_len) {
    if (!engine || !frag_data) {
        return REASSEMBLY_ERROR;
    }
    if (frag_len < DASHCUE_FRAGMENT_HEADER_SIZE) {
        return REASSEMBLY_ERROR;
    }

    const uint8_t lane = frag_data[0];
    const uint8_t total_frags = frag_data[1];
    const uint8_t frag_idx = frag_data[2];

    if (lane >= DASHCUE_NUM_LANES || total_frags == 0 || frag_idx >= total_frags) {
        return REASSEMBLY_ERROR;
    }

    const size_t chunk_len = frag_len - DASHCUE_FRAGMENT_HEADER_SIZE;
    ReassemblyLane *l = &engine->lanes[lane];

    // Check timeout on active lane
    if (l->active && (now_ms - l->last_frag_time_ms >= DASHCUE_REASSEMBLY_TIMEOUT_MS)) {
        engine->frag_timeouts_count++;
        reassembly_reset_lane(engine, lane);
        if (frag_idx != 0) {
            return REASSEMBLY_TIMEOUT;
        }
    }

    // Sequence check:
    // If lane is not active: only frag 0 can initiate a message
    if (!l->active) {
        if (frag_idx != 0) {
            engine->out_of_order_count++;
            return REASSEMBLY_ERROR;
        }

        // The final fragment may be short. Reject only when even the minimum
        // total implied by full non-final fragments cannot fit. Every actual
        // append is separately bounded below, so no claimed length is trusted.
        if (chunk_len > DASHCUE_MAX_FRAME_SIZE ||
            (total_frags > 1 &&
             (size_t)(total_frags - 1) * chunk_len >= DASHCUE_MAX_FRAME_SIZE)) {
            engine->length_violation_count++;
            return REASSEMBLY_ERROR;
        }

        l->active = true;
        l->total_frags = total_frags;
        l->next_frag_idx = 1;
        l->received_bytes = 0;
        l->last_frag_time_ms = now_ms;
    } else {
        // Lane is already active: incoming fragment must strictly match next_frag_idx
        if (l->total_frags != total_frags || frag_idx != l->next_frag_idx) {
            // Out of order, duplicate (e.g. frag 0 again), or orphaned fragment
            engine->out_of_order_count++;
            reassembly_reset_lane(engine, lane);
            return REASSEMBLY_ERROR;
        }
        l->next_frag_idx++;
        l->last_frag_time_ms = now_ms;
    }

    // Length boundary protection
    if (l->received_bytes + chunk_len > DASHCUE_MAX_FRAME_SIZE) {
        engine->length_violation_count++;
        reassembly_reset_lane(engine, lane);
        return REASSEMBLY_ERROR;
    }

    // Copy chunk payload into lane buffer
    if (chunk_len > 0) {
        memcpy(&l->buffer[l->received_bytes], &frag_data[DASHCUE_FRAGMENT_HEADER_SIZE], chunk_len);
        l->received_bytes += chunk_len;
    }

    // Check completion
    if (l->next_frag_idx >= l->total_frags) {
        if (out_msg_bytes) {
            *out_msg_bytes = l->buffer;
        }
        if (out_msg_len) {
            *out_msg_len = l->received_bytes;
        }
        l->active = false;
        return REASSEMBLY_COMPLETE;
    }

    return REASSEMBLY_IN_PROGRESS;
}

// --- Flow Control Implementation ---

void flow_control_init(CreditFlowController *fc, uint8_t initial_credits) {
    if (fc) {
        fc->available_credits = initial_credits;
    }
}

bool flow_control_can_send(const CreditFlowController *fc, uint8_t lane) {
    if (!fc) {
        return false;
    }
    // Control lane has a reserved credit and is never starved
    if (lane == DASHCUE_LANE_CONTROL) {
        return true;
    }
    return fc->available_credits > 0;
}

bool flow_control_consume(CreditFlowController *fc, uint8_t lane) {
    if (!fc) {
        return false;
    }
    // VULN-PR-04: Credit decrement accounting for control and bulk lanes
    if (fc->available_credits > 0) {
        fc->available_credits--;
        return true;
    }
    // Control lane has 1 reserved credit and can send even when credits reach 0
    if (lane == DASHCUE_LANE_CONTROL) {
        return true;
    }
    return false;
}

void flow_control_grant(CreditFlowController *fc, uint8_t credits) {
    if (!fc) {
        return;
    }
    uint16_t updated = (uint16_t)fc->available_credits + credits;
    if (updated > DASHCUE_INITIAL_CREDITS) {
        updated = DASHCUE_INITIAL_CREDITS;
    }
    fc->available_credits = (uint8_t)updated;
}

uint8_t flow_control_get_credits(const CreditFlowController *fc) {
    return fc ? fc->available_credits : 0;
}
