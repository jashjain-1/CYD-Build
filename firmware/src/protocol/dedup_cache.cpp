#include "dedup_cache.h"
#include <string.h>

void dedup_init(DedupCache *cache) {
    if (!cache) {
        return;
    }
    memset(cache, 0, sizeof(*cache));
}

bool dedup_check_and_record(DedupCache *cache,
                            uint32_t command_id,
                            uint64_t now_ms,
                            dashcue_v1_CmdResult *out_cached_result) {
    if (!cache) {
        return false;
    }

    // 1. Search for active entry matching command_id
    for (size_t i = 0; i < DASHCUE_DEDUP_CACHE_SIZE; ++i) {
        DedupEntry *e = &cache->entries[i];
        if (e->valid && e->command_id == command_id) {
            if (now_ms >= e->timestamp_ms && (now_ms - e->timestamp_ms) <= DASHCUE_DEDUP_TTL_MS) {
                // Active duplicate found! Replay cached result
                cache->duplicate_commands_count++;
                if (out_cached_result) {
                    out_cached_result->command_id = e->command_id;
                    out_cached_result->status = e->status;
                    out_cached_result->reason = e->reason;
                    out_cached_result->observed_at_ms = e->observed_at_ms;
                }
                return true;
            }
            // Entry has expired (> 60s); invalidate it
            e->valid = false;
        }
    }

    // 2. New command: find slot (invalid/expired or oldest)
    size_t victim_idx = 0;
    uint64_t oldest_time = UINT64_MAX;

    for (size_t i = 0; i < DASHCUE_DEDUP_CACHE_SIZE; ++i) {
        DedupEntry *e = &cache->entries[i];
        if (!e->valid || (now_ms >= e->timestamp_ms && (now_ms - e->timestamp_ms) > DASHCUE_DEDUP_TTL_MS)) {
            victim_idx = i;
            break;
        }
        if (e->timestamp_ms < oldest_time) {
            oldest_time = e->timestamp_ms;
            victim_idx = i;
        }
    }

    // Store new preliminary entry in victim_idx
    DedupEntry *slot = &cache->entries[victim_idx];
    slot->command_id = command_id;
    slot->timestamp_ms = now_ms;
    slot->status = dashcue_v1_CmdResult_Status_ST_ACCEPTED;
    slot->reason = dashcue_v1_ErrorCode_EC_NONE;
    slot->observed_at_ms = now_ms;
    slot->valid = true;

    return false;
}

bool dedup_update_result(DedupCache *cache,
                         uint32_t command_id,
                         dashcue_v1_CmdResult_Status status,
                         uint32_t reason,
                         uint64_t observed_at_ms) {
    if (!cache) {
        return false;
    }
    for (size_t i = 0; i < DASHCUE_DEDUP_CACHE_SIZE; ++i) {
        DedupEntry *e = &cache->entries[i];
        if (e->valid && e->command_id == command_id) {
            e->status = status;
            e->reason = reason;
            e->observed_at_ms = observed_at_ms;
            return true;
        }
    }
    return false;
}

uint32_t dedup_get_duplicate_count(const DedupCache *cache) {
    return cache ? cache->duplicate_commands_count : 0;
}
