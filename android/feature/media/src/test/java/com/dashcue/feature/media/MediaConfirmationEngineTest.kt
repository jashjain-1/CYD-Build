package com.dashcue.feature.media

import com.dashcue.v1.*
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.test.*
import org.junit.Assert.*
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class MediaConfirmationEngineTest {

    @Test
    fun testSuccessfulObservedConfirmation() = runTest {
        val engine = MediaConfirmationEngine(this)
        val emittedResults = mutableListOf<CmdResult>()
        val job = backgroundScope.launch {
            engine.cmdResults.collect { emittedResults.add(it) }
        }
        testScheduler.runCurrent()

        val caps = MediaCapabilityCalculator.calculateCapabilities(
            android.media.session.PlaybackState.ACTION_PLAY or android.media.session.PlaybackState.ACTION_PAUSE
        )
        val cmd = MediaCmd.newBuilder()
            .setCommandId(5001)
            .setAction(MediaAction.MA_PLAY)
            .setSnapshotVersion(42)
            .build()

        var actionExecuted = false
        val accepted = engine.submitCommand(cmd, currentSnapshotVersion = 42, capabilities = caps) { action, _ ->
            if (action == MediaAction.MA_PLAY) {
                actionExecuted = true
                true
            } else false
        }

        assertTrue(accepted)
        assertTrue(actionExecuted)
        testScheduler.runCurrent()

        // Verify ST_ACCEPTED was emitted
        assertEquals(1, emittedResults.size)
        assertEquals(CmdResult.Status.ST_ACCEPTED, emittedResults[0].status)
        assertEquals(5001, emittedResults[0].commandId)

        // Observe playback state changed to PB_PLAYING
        engine.onObservedStateChange(PlaybackStatus.PB_PLAYING)
        testScheduler.runCurrent()

        // Verify ST_CONFIRMED was emitted
        assertEquals(2, emittedResults.size)
        assertEquals(CmdResult.Status.ST_CONFIRMED, emittedResults[1].status)
        assertEquals(5001, emittedResults[1].commandId)

        job.cancel()
    }

    @Test
    fun testUnconfirmedOnTimeout() = runTest {
        // TC-AN-05: Player response delays > timeout -> ST_UNCONFIRMED on timeout
        val engine = MediaConfirmationEngine(this)
        val emittedResults = mutableListOf<CmdResult>()
        val job = backgroundScope.launch {
            engine.cmdResults.collect { emittedResults.add(it) }
        }

        val caps = MediaCapabilityCalculator.calculateCapabilities(android.media.session.PlaybackState.ACTION_PLAY)
        val cmd = MediaCmd.newBuilder()
            .setCommandId(5002)
            .setAction(MediaAction.MA_PLAY)
            .setSnapshotVersion(10)
            .build()

        engine.submitCommand(cmd, currentSnapshotVersion = 10, capabilities = caps) { _, _ -> true }
        testScheduler.runCurrent()
        assertEquals(1, emittedResults.size)
        assertEquals(CmdResult.Status.ST_ACCEPTED, emittedResults[0].status)

        // Advance past 1500ms timeout
        testScheduler.advanceTimeBy(1600)
        testScheduler.runCurrent()

        // Should emit ST_UNCONFIRMED
        assertEquals(2, emittedResults.size)
        assertEquals(CmdResult.Status.ST_UNCONFIRMED, emittedResults[1].status)
        assertEquals(5002, emittedResults[1].commandId)

        job.cancel()
    }

    @Test
    fun testStaleSnapshotRejected() = runTest {
        val engine = MediaConfirmationEngine(this)
        val emittedResults = mutableListOf<CmdResult>()
        val job = backgroundScope.launch {
            engine.cmdResults.collect { emittedResults.add(it) }
        }

        val caps = MediaCapabilityCalculator.calculateCapabilities(android.media.session.PlaybackState.ACTION_PLAY)
        val cmd = MediaCmd.newBuilder()
            .setCommandId(5003)
            .setAction(MediaAction.MA_PLAY)
            .setSnapshotVersion(5) // Stale: current is 10
            .build()

        val accepted = engine.submitCommand(cmd, currentSnapshotVersion = 10, capabilities = caps) { _, _ -> true }
        assertFalse(accepted)
        testScheduler.runCurrent()

        assertEquals(1, emittedResults.size)
        assertEquals(CmdResult.Status.ST_FAILED, emittedResults[0].status)
        assertEquals(ErrorCode.EC_STALE_VALUE, emittedResults[0].reason)

        job.cancel()
    }

    @Test
    fun testUnsupportedActionRejected() = runTest {
        val engine = MediaConfirmationEngine(this)
        val emittedResults = mutableListOf<CmdResult>()
        val job = backgroundScope.launch {
            engine.cmdResults.collect { emittedResults.add(it) }
        }

        val caps = MediaCapabilityCalculator.calculateCapabilities(android.media.session.PlaybackState.ACTION_PLAY)
        // Attempt to SKIP_TO_NEXT when only PLAY is supported
        val cmd = MediaCmd.newBuilder()
            .setCommandId(5004)
            .setAction(MediaAction.MA_NEXT)
            .setSnapshotVersion(1)
            .build()

        val accepted = engine.submitCommand(cmd, currentSnapshotVersion = 1, capabilities = caps) { _, _ -> true }
        assertFalse(accepted)
        testScheduler.runCurrent()

        assertEquals(1, emittedResults.size)
        assertEquals(CmdResult.Status.ST_FAILED, emittedResults[0].status)
        assertEquals(ErrorCode.EC_UNSUPPORTED_VALUE, emittedResults[0].reason)

        job.cancel()
    }
}
