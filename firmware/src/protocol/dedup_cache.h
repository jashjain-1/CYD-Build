#ifndef DASHCUE_PROTOCOL_DEDUP_CACHE_H
#define DASHCUE_PROTOCOL_DEDUP_CACHE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "generated/dashcue.pb.h"

#define DASHCUE_DEDUP_CACHE_SIZE   32u
#define DASHCUE_DEDUP_TTL_MS       60000u // 60.0 seconds

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t command_id;
    uint64_t timestamp_ms;
    dashcue_v1_CmdResult_Status status;
    uint32_t reason;
    uint64_t observed_at_ms;
    bool valid;
} DedupEntry;

typedef struct {
    DedupEntry entries[DASHCUE_DEDUP_CACHE_SIZE];
    uint32_t duplicate_commands_count;
} DedupCache;

/**
 * Initialize deduplication cache.
 */
void dedup_init(DedupCache *cache);

/**
 * Check if command_id is in cache within 60.0 s TTL.
 * If found (duplicate): increments duplicate counter, populates out_cached_result, returns true.
 * If not found (new): allocates slot, sets preliminary ST_ACCEPTED, returns false.
 *
 * @param cache Pointer to DedupCache.
 * @param command_id Command ID to check.
 * @param now_ms Current monotonic millisecond timestamp.
 * @param out_cached_result Output: cached result populated if duplicate.
 * @return true if duplicate command, false if newly recorded command.
 */
bool dedup_check_and_record(DedupCache *cache,
                            uint32_t command_id,
                            uint64_t now_ms,
                            dashcue_v1_CmdResult *out_cached_result);

/**
 * Update the execution result of an existing command in cache.
 *
 * @param cache Pointer to DedupCache.
 * @param command_id Command ID to update.
 * @param status Final status (e.g. ST_CONFIRMED, ST_FAILED).
 * @param reason ErrorCode reason (if failed).
 * @param observed_at_ms Clock timestamp when state change was observed.
 * @return true if updated, false if command_id not in cache.
 */
bool dedup_update_result(DedupCache *cache,
                         uint32_t command_id,
                         dashcue_v1_CmdResult_Status status,
                         uint32_t reason,
                         uint64_t observed_at_ms);

/**
 * Get total number of duplicate commands detected.
 */
uint32_t dedup_get_duplicate_count(const DedupCache *cache);

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_PROTOCOL_DEDUP_CACHE_H
