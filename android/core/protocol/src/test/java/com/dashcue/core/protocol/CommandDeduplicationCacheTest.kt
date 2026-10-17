package com.dashcue.core.protocol

import org.junit.Assert.*
import org.junit.Test

class CommandDeduplicationCacheTest {

    @Test
    fun testCacheHitWithinTtl() {
        // TC-PR-08: Re-transmit identical command_id within 60s -> cached CmdResult replayed
        val cache = CommandDeduplicationCache(maxCapacity = 32, ttlMs = 60_000L)
        val cmdId = 5001L
        val resultPayload = "CmdResultAccepted".toByteArray()

        val t0 = 1000L
        cache.put(cmdId, resultPayload, currentTimeMs = t0)

        // Lookup at t0 + 30 seconds (within 60s TTL)
        val cached = cache.lookup(cmdId, currentTimeMs = t0 + 30_000L)
        assertNotNull("Cached result must be returned within TTL", cached)
        assertArrayEquals(resultPayload, cached)
    }

    @Test
    fun testCacheMissAfterTtl() {
        val cache = CommandDeduplicationCache(maxCapacity = 32, ttlMs = 60_000L)
        val cmdId = 5002L
        val resultPayload = "CmdResultConfirmed".toByteArray()

        val t0 = 1000L
        cache.put(cmdId, resultPayload, currentTimeMs = t0)

        // Lookup at t0 + 61 seconds (expired)
        val cached = cache.lookup(cmdId, currentTimeMs = t0 + 61_000L)
        assertNull("Cached result must expire after TTL", cached)
    }

    @Test
    fun testSlidingWindowCapacityLimit() {
        // Sliding window of 32 slots: inserting 33 items evicts the oldest
        val cache = CommandDeduplicationCache(maxCapacity = 32, ttlMs = 60_000L)
        val t0 = 1000L

        for (i in 1..32) {
            cache.put(i.toLong(), "Result_$i".toByteArray(), currentTimeMs = t0 + i)
        }
        assertEquals(32, cache.size())

        // Command 1 should be present
        assertNotNull(cache.lookup(1L, currentTimeMs = t0 + 100))

        // Put 33rd item
        cache.put(33L, "Result_33".toByteArray(), currentTimeMs = t0 + 100)
        assertEquals(32, cache.size())

        // Command 1 was the eldest, should have been evicted
        assertNull("Command 1 should have been evicted", cache.lookup(1L, currentTimeMs = t0 + 100))
        assertNotNull("Command 2 should still be present", cache.lookup(2L, currentTimeMs = t0 + 100))
        assertNotNull("Command 33 should be present", cache.lookup(33L, currentTimeMs = t0 + 100))
    }
}
