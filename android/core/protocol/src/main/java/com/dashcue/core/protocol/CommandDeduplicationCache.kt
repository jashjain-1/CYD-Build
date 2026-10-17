package com.dashcue.core.protocol

import java.util.LinkedHashMap

/**
 * Command deduplication cache (spec §9.5, TC-PR-08).
 *
 * Sliding window of 32 command IDs with 60-second TTL.
 * Replays cached CmdResult for duplicate command IDs within 60 seconds
 * without re-executing actions.
 */
class CommandDeduplicationCache(
    val maxCapacity: Int = 32,
    val ttlMs: Long = 60_000L
) {
    data class Entry(
        val commandId: Long,
        val resultPayload: ByteArray,
        val recordedAtMs: Long
    )

    private val cache = object : LinkedHashMap<Long, Entry>(maxCapacity, 0.75f, false) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<Long, Entry>?): Boolean {
            return size > maxCapacity
        }
    }

    /**
     * Looks up a cached command result.
     * Returns the cached payload if found and not expired.
     */
    @Synchronized
    fun lookup(commandId: Long, currentTimeMs: Long = System.currentTimeMillis()): ByteArray? {
        val entry = cache[commandId] ?: return null
        if (currentTimeMs - entry.recordedAtMs > ttlMs) {
            cache.remove(commandId)
            return null
        }
        return entry.resultPayload
    }

    /**
     * Stores a command result in the cache.
     */
    @Synchronized
    fun put(
        commandId: Long,
        resultPayload: ByteArray,
        currentTimeMs: Long = System.currentTimeMillis()
    ) {
        // Evict expired entries if any
        val iterator = cache.entries.iterator()
        while (iterator.hasNext()) {
            val item = iterator.next().value
            if (currentTimeMs - item.recordedAtMs > ttlMs) {
                iterator.remove()
            }
        }

        cache[commandId] = Entry(commandId, resultPayload, currentTimeMs)
    }

    @Synchronized
    fun size(): Int = cache.size

    @Synchronized
    fun clear() {
        cache.clear()
    }
}
