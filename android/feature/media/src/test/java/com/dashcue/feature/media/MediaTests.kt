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
            currentSnapshotVersion = 2L, // Stale! 1 != 2
            onResult = { result = it }
        )

        assertNotNull(result)
        assertEquals(CmdResult.Status.ST_FAILED, result?.status)
        assertEquals(MediaCommandDispatcher.EC_STALE, result?.reason)
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
        assertEquals(CmdResult.Status.ST_ACCEPTED, results[0].status)
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
