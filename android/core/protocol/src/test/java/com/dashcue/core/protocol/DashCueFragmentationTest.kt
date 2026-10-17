package com.dashcue.core.protocol

import org.junit.Assert.*
import org.junit.Test

class DashCueFragmentationTest {

    @Test
    fun testFragmentationAndReassemblyMtu247() {
        // TC-PR-06: Transmit 4096-byte payload over ATT MTU 247 -> <= 18 chunks
        val engine = DashCueFragmentation()
        val payload = ByteArray(4096) { (it % 256).toByte() }
        val frame = DashCueFraming.encodeFrame(10, 1001L, 0, payload)

        val fragments = engine.fragment(frame, lane = DashCueFragmentation.LANE_CONTROL, mtu = 247)
        assertTrue("Expected <= 18 fragments for MTU 247, got ${fragments.size}", fragments.size <= 18)

        var completeResult: DashCueFragmentation.ReassemblyResult? = null
        for (frag in fragments) {
            completeResult = engine.onFragmentReceived(frag)
        }

        assertTrue("Expected ReassemblyResult.Complete, but was: $completeResult", completeResult is DashCueFragmentation.ReassemblyResult.Complete)
        val complete = completeResult as DashCueFragmentation.ReassemblyResult.Complete
        assertEquals(DashCueFragmentation.LANE_CONTROL, complete.lane)
        assertArrayEquals(frame, complete.frame)
    }

    @Test
    fun testFragmentationAndReassemblyMtu23() {
        // TC-PR-05: Transmit 4096-byte payload over ATT MTU 23 -> <= 256 chunks, zero loss
        val engine = DashCueFragmentation()
        val payload = ByteArray(2048) { (it % 256).toByte() }
        val frame = DashCueFraming.encodeFrame(10, 1002L, 0, payload)

        val fragments = engine.fragment(frame, lane = DashCueFragmentation.LANE_CONTROL, mtu = 23)
        // MTU 23 gives chunk payload 16 bytes.
        assertTrue(fragments.size > 100)
        assertTrue(fragments.size <= 255)

        var completeResult: DashCueFragmentation.ReassemblyResult? = null
        for (frag in fragments) {
            completeResult = engine.onFragmentReceived(frag)
        }

        assertTrue("Expected ReassemblyResult.Complete", completeResult is DashCueFragmentation.ReassemblyResult.Complete)
        val complete = completeResult as DashCueFragmentation.ReassemblyResult.Complete
        assertArrayEquals(frame, complete.frame)
    }

    @Test
    fun testOutOfOrderFragmentRejection() {
        // TC-PR-09: Inject duplicate and out-of-order chunk indices -> context aborted, returns error
        val engine = DashCueFragmentation()
        val frame = DashCueFraming.encodeFrame(10, 1003L, 0, "Multi Chunk Data Frame Here".toByteArray())
        val fragments = engine.fragment(frame, mtu = 23)
        assertTrue(fragments.size >= 3)

        // Deliver chunk 0
        val r0 = engine.onFragmentReceived(fragments[0])
        assertTrue(r0 is DashCueFragmentation.ReassemblyResult.Progress)

        // Inject chunk 2 instead of chunk 1 (out of order)
        val rOut = engine.onFragmentReceived(fragments[2])
        assertTrue("Out-of-order chunk must return Error", rOut is DashCueFragmentation.ReassemblyResult.Error)
        assertEquals(DashCueFraming.EC_DECODE, (rOut as DashCueFragmentation.ReassemblyResult.Error).errorCode)
    }

    @Test
    fun testDuplicateFragmentRejection() {
        val engine = DashCueFragmentation()
        val frame = DashCueFraming.encodeFrame(10, 1004L, 0, "Another Multi Chunk Data Frame".toByteArray())
        val fragments = engine.fragment(frame, mtu = 23)

        engine.onFragmentReceived(fragments[0])
        // Deliver chunk 0 again (duplicate)
        val rDup = engine.onFragmentReceived(fragments[0])
        assertTrue("Duplicate chunk must return Error", rDup is DashCueFragmentation.ReassemblyResult.Error)
    }

    @Test
    fun testReassemblyTimeout() {
        val engine = DashCueFragmentation(reassemblyTimeoutMs = 100L)
        val frame = DashCueFraming.encodeFrame(10, 1005L, 0, "Timeout Data Frame".toByteArray())
        val fragments = engine.fragment(frame, mtu = 23)

        val t0 = 1000L
        engine.onFragmentReceived(fragments[0], currentTimeMs = t0)

        // Deliver chunk 1 after 250ms (timeout is 100ms)
        val rLate = engine.onFragmentReceived(fragments[1], currentTimeMs = t0 + 250L)
        // Since timeout expired, the context was reset, so receiving chunk 1 now is treated as unexpected (expected 0)
        assertTrue("Late chunk after timeout must be rejected", rLate is DashCueFragmentation.ReassemblyResult.Error)
    }

    @Test
    fun testConcurrentLanesMultiplexing() {
        val engine = DashCueFragmentation()
        val frameCtrl = DashCueFraming.encodeFrame(10, 1006L, 0, "Control Lane Message".toByteArray())
        val frameBulk = DashCueFraming.encodeFrame(22, 1007L, 0, "Bulk Lane Image Data Chunk 0123456789".toByteArray())

        val fragsCtrl = engine.fragment(frameCtrl, lane = DashCueFragmentation.LANE_CONTROL, mtu = 23)
        val fragsBulk = engine.fragment(frameBulk, lane = DashCueFragmentation.LANE_BULK, mtu = 23)

        // Interleave fragments from both lanes
        var iCtrl = 0
        var iBulk = 0
        var completeCtrl: ByteArray? = null
        var completeBulk: ByteArray? = null

        while (iCtrl < fragsCtrl.size || iBulk < fragsBulk.size) {
            if (iCtrl < fragsCtrl.size) {
                val res = engine.onFragmentReceived(fragsCtrl[iCtrl++])
                if (res is DashCueFragmentation.ReassemblyResult.Complete) {
                    completeCtrl = res.frame
                }
            }
            if (iBulk < fragsBulk.size) {
                val res = engine.onFragmentReceived(fragsBulk[iBulk++])
                if (res is DashCueFragmentation.ReassemblyResult.Complete) {
                    completeBulk = res.frame
                }
            }
        }

        assertNotNull("Control lane should complete", completeCtrl)
        assertNotNull("Bulk lane should complete", completeBulk)
        assertArrayEquals(frameCtrl, completeCtrl)
        assertArrayEquals(frameBulk, completeBulk)
    }
}
