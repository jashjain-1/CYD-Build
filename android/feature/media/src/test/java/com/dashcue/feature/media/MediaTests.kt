package com.dashcue.feature.media

import android.media.session.MediaController
import android.media.session.PlaybackState
import com.dashcue.v1.CmdResult
import com.dashcue.v1.MediaAction
import com.dashcue.v1.MediaCmd
import com.dashcue.v1.PlaybackStatus
import io.mockk.*
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class MediaTests {

    @Test
    fun testCapabilityBitmaskMapping() {
        val actions = PlaybackState.ACTION_PLAY or
                PlaybackState.ACTION_PAUSE or
                PlaybackState.ACTION_SKIP_TO_NEXT or
                PlaybackState.ACTION_SKIP_TO_PREVIOUS or
                PlaybackState.ACTION_SEEK_TO

        val mask = MediaActionMapper.mapCapabilities(actions)
        assertTrue((mask and (1 shl MediaAction.MA_PLAY_VALUE)) != 0)
        assertTrue((mask and (1 shl MediaAction.MA_PAUSE_VALUE)) != 0)
        assertTrue((mask and (1 shl MediaAction.MA_NEXT_VALUE)) != 0)
        assertTrue((mask and (1 shl MediaAction.MA_PREV_VALUE)) != 0)
        assertTrue((mask and (1 shl MediaAction.MA_SEEK_REL_VALUE)) != 0)
        assertTrue((mask and (1 shl MediaAction.MA_SEEK_ABS_VALUE)) != 0)
    }

    @Test
    fun testPlaybackStatusMapping() {
        assertEquals(PlaybackStatus.PB_PLAYING, MediaActionMapper.mapPlaybackStatus(PlaybackState.STATE_PLAYING))
        assertEquals(PlaybackStatus.PB_PAUSED, MediaActionMapper.mapPlaybackStatus(PlaybackState.STATE_PAUSED))
        assertEquals(PlaybackStatus.PB_BUFFERING, MediaActionMapper.mapPlaybackStatus(PlaybackState.STATE_BUFFERING))
        assertEquals(PlaybackStatus.PB_STOPPED, MediaActionMapper.mapPlaybackStatus(PlaybackState.STATE_STOPPED))
        assertEquals(PlaybackStatus.PB_NO_SESSION, MediaActionMapper.mapPlaybackStatus(PlaybackState.STATE_NONE))
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun testMediaCommandDispatcherStaleVersionRejection() = runTest {
        val dispatcher = MediaCommandDispatcher(this)
        val controller = mockk<MediaController>(relaxed = true)

        val cmd = MediaCmd.newBuilder()
            .setCommandId(101)
            .setAction(MediaAction.MA_PLAY)
            .setSnapshotVersion(1)
            .build()

        var result: CmdResult? = null
        dispatcher.dispatchCommand(
            cmd = cmd,
            controller = controller,
            currentSnapshotVersion = 4L, // Stale: delta = 4 - 1 = 3 > 2 tolerance window
            onResult = { result = it }
        )

        assertNotNull(result)
        assertEquals(CmdResult.Status.ST_FAILED, result?.status)
        assertEquals(MediaCommandDispatcher.EC_STALE, result?.reason)
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun testMediaCommandToleranceWindowAllowsLagOfTwoVersions() = runTest {
        val dispatcher = MediaCommandDispatcher(scope = this, actionExecutor = { _, _, _ -> })
        val controller = mockk<MediaController>(relaxed = true)

        val cmd = MediaCmd.newBuilder()
            .setCommandId(102)
            .setAction(MediaAction.MA_PLAY)
            .setSnapshotVersion(2)
            .build()

        val results = mutableListOf<CmdResult>()
        dispatcher.dispatchCommand(
            cmd = cmd,
            controller = controller,
            currentSnapshotVersion = 4L, // delta = 4 - 2 = 2 <= 2 (within tolerance window)
            onResult = { results.add(it) }
        )

        assertEquals(1, results.size)
        assertEquals(CmdResult.Status.ST_ACCEPTED, results[0].status)
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun testMediaCommandZeroSnapshotVersionBypassesStaleCheck() = runTest {
        val dispatcher = MediaCommandDispatcher(scope = this, actionExecutor = { _, _, _ -> })
        val controller = mockk<MediaController>(relaxed = true)

        val cmd = MediaCmd.newBuilder()
            .setCommandId(103)
            .setAction(MediaAction.MA_PAUSE)
            .setSnapshotVersion(0) // version 0 bypasses stale check
            .build()

        val results = mutableListOf<CmdResult>()
        dispatcher.dispatchCommand(
            cmd = cmd,
            controller = controller,
            currentSnapshotVersion = 20L,
            onResult = { results.add(it) }
        )

        assertEquals(1, results.size)
        assertEquals(CmdResult.Status.ST_ACCEPTED, results[0].status)
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun testMediaCommandExplicitPlayAndObservedConfirmation() = runTest {
        var playCalled = false
        var pauseCalled = false
        val dispatcher = MediaCommandDispatcher(
            scope = this,
            actionExecutor = { action, _, _ ->
                if (action == MediaAction.MA_PLAY) playCalled = true
                if (action == MediaAction.MA_PAUSE) pauseCalled = true
            }
        )
        val controller = mockk<MediaController>(relaxed = true)

        val cmd = MediaCmd.newBuilder()
            .setCommandId(202)
            .setAction(MediaAction.MA_PLAY)
            .setSnapshotVersion(5)
            .build()

        val results = mutableListOf<CmdResult>()
        dispatcher.dispatchCommand(
            cmd = cmd,
            controller = controller,
            currentSnapshotVersion = 5L,
            onResult = { results.add(it) }
        )

        // Verify explicit transport.play() was invoked (never a toggle!)
        assertTrue(playCalled)
        assertFalse(pauseCalled)

        // Initial result should be ST_ACCEPTED
        assertEquals(1, results.size)
        assertEquals(CmdResult.Status.ST_ACCEPTED, results[0].status)

        // Simulate observing PlaybackState change to STATE_PLAYING
        val observedState = PlaybackState.Builder()
            .setState(PlaybackState.STATE_PLAYING, 0L, 1.0f)
            .build()
        dispatcher.onObservedPlaybackState(observedState)

        // Result should now include ST_CONFIRMED
        assertEquals(2, results.size)
        assertEquals(CmdResult.Status.ST_CONFIRMED, results[1].status)
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun testVolumeCommandsBypassSnapshotVersionAndConfirmImmediately() = runTest {
        val audioManager = mockk<android.media.AudioManager>(relaxed = true)
        val dispatcher = MediaCommandDispatcher(scope = this, audioManager = audioManager)
        val controller = mockk<MediaController>(relaxed = true)

        val stepCmd = MediaCmd.newBuilder()
            .setCommandId(201)
            .setAction(MediaAction.MA_VOL_STEP)
            .setArg(1)
            .setSnapshotVersion(1)
            .build()

        val muteCmd = MediaCmd.newBuilder()
            .setCommandId(202)
            .setAction(MediaAction.MA_MUTE_SET)
            .setArg(1)
            .setSnapshotVersion(1)
            .build()

        val stepResults = mutableListOf<CmdResult>()
        dispatcher.dispatchCommand(
            cmd = stepCmd,
            controller = controller,
            currentSnapshotVersion = 99L, // Completely mismatching snapshot, but volume bypasses it
            onResult = { stepResults.add(it) }
        )

        val muteResults = mutableListOf<CmdResult>()
        dispatcher.dispatchCommand(
            cmd = muteCmd,
            controller = controller,
            currentSnapshotVersion = 99L,
            onResult = { muteResults.add(it) }
        )

        assertEquals(1, stepResults.size)
        assertEquals(CmdResult.Status.ST_CONFIRMED, stepResults[0].status)
        assertEquals(1, muteResults.size)
        assertEquals(CmdResult.Status.ST_CONFIRMED, muteResults[0].status)
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun testObservedTrackChangeConfirmsNextAndPrev() = runTest {
        val dispatcher = MediaCommandDispatcher(scope = this, actionExecutor = { _, _, _ -> })
        val controller = mockk<MediaController>(relaxed = true)

        val nextCmd = MediaCmd.newBuilder()
            .setCommandId(301)
            .setAction(MediaAction.MA_NEXT)
            .setSnapshotVersion(5)
            .build()

        val prevCmd = MediaCmd.newBuilder()
            .setCommandId(302)
            .setAction(MediaAction.MA_PREV)
            .setSnapshotVersion(5)
            .build()

        val nextResults = mutableListOf<CmdResult>()
        dispatcher.dispatchCommand(nextCmd, controller, 5L) { nextResults.add(it) }

        val prevResults = mutableListOf<CmdResult>()
        dispatcher.dispatchCommand(prevCmd, controller, 5L) { prevResults.add(it) }

        assertEquals(1, nextResults.size)
        assertEquals(CmdResult.Status.ST_ACCEPTED, nextResults[0].status)
        assertEquals(1, prevResults.size)
        assertEquals(CmdResult.Status.ST_ACCEPTED, prevResults[0].status)

        // Metadata changed triggers onObservedTrackChange
        dispatcher.onObservedTrackChange()

        assertEquals(2, nextResults.size)
        assertEquals(CmdResult.Status.ST_CONFIRMED, nextResults[1].status)
        assertEquals(2, prevResults.size)
        assertEquals(CmdResult.Status.ST_CONFIRMED, prevResults[1].status)
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun testPlayPredicateAcceptsBufferingAndConnecting() = runTest {
        val dispatcher = MediaCommandDispatcher(scope = this, actionExecutor = { _, _, _ -> })
        val controller = mockk<MediaController>(relaxed = true)

        val cmd = MediaCmd.newBuilder()
            .setCommandId(401)
            .setAction(MediaAction.MA_PLAY)
            .setSnapshotVersion(1)
            .build()

        val results = mutableListOf<CmdResult>()
        dispatcher.dispatchCommand(cmd, controller, 1L) { results.add(it) }

        assertEquals(1, results.size)
        assertEquals(CmdResult.Status.ST_ACCEPTED, results[0].status)

        // Player transitions to STATE_BUFFERING
        val bufferingState = PlaybackState.Builder()
            .setState(PlaybackState.STATE_BUFFERING, 0L, 0f)
            .build()
        dispatcher.onObservedPlaybackState(bufferingState)

        assertEquals(2, results.size)
        assertEquals(CmdResult.Status.ST_CONFIRMED, results[1].status)
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun testPlayWhenAlreadyPlayingConfirmsImmediately() = runTest {
        var playCalled = false
        val dispatcher = MediaCommandDispatcher(
            scope = this,
            actionExecutor = { action, _, _ -> if (action == MediaAction.MA_PLAY) playCalled = true }
        )
        val controller = mockk<MediaController>(relaxed = true)
        val playingState = PlaybackState.Builder()
            .setState(PlaybackState.STATE_PLAYING, 1000L, 1.0f)
            .build()
        every { controller.playbackState } returns playingState

        val cmd = MediaCmd.newBuilder()
            .setCommandId(402)
            .setAction(MediaAction.MA_PLAY)
            .setSnapshotVersion(1)
            .build()

        val results = mutableListOf<CmdResult>()
        dispatcher.dispatchCommand(cmd, controller, 1L) { results.add(it) }

        assertEquals(1, results.size)
        assertEquals(CmdResult.Status.ST_CONFIRMED, results[0].status)
        assertTrue(playCalled)
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun testMediaCommandDispatcherVolSet() = runTest {
        val audioManager = mockk<android.media.AudioManager>(relaxed = true)
        every { audioManager.getStreamMaxVolume(android.media.AudioManager.STREAM_MUSIC) } returns 100
        val dispatcher = MediaCommandDispatcher(scope = this, audioManager = audioManager)
        val controller = mockk<MediaController>(relaxed = true)

        val cmd = MediaCmd.newBuilder()
            .setCommandId(303)
            .setAction(MediaAction.MA_VOL_SET)
            .setArg(75)
            .setSnapshotVersion(1)
            .build()

        val results = mutableListOf<CmdResult>()
        dispatcher.dispatchCommand(
            cmd = cmd,
            controller = controller,
            currentSnapshotVersion = 1L,
            onResult = { results.add(it) }
        )

        verify { audioManager.setStreamVolume(android.media.AudioManager.STREAM_MUSIC, 75, 0) }
        assertEquals(1, results.size)
        assertEquals(CmdResult.Status.ST_CONFIRMED, results[0].status)
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun testMediaCommandDispatcherExceptionGracefullyHandled() = runTest {
        val dispatcher = MediaCommandDispatcher(
            scope = this,
            actionExecutor = { _, _, _ -> throw android.os.DeadObjectException() }
        )
        val controller = mockk<MediaController>(relaxed = true)

        val cmd = MediaCmd.newBuilder()
            .setCommandId(404)
            .setAction(MediaAction.MA_PLAY)
            .setSnapshotVersion(1)
            .build()

        var result: CmdResult? = null
        dispatcher.dispatchCommand(
            cmd = cmd,
            controller = controller,
            currentSnapshotVersion = 1L,
            onResult = { result = it }
        )

        assertNotNull(result)
        assertEquals(CmdResult.Status.ST_FAILED, result?.status)
        assertEquals(MediaCommandDispatcher.EC_NO_SESSION, result?.reason)
    }
}
