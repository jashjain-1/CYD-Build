package com.dashcue.feature.media

import android.media.AudioManager
import android.media.session.MediaController
import android.media.session.PlaybackState
import com.dashcue.core.common.DashCueLog
import com.dashcue.core.protocol.DashCueFraming
import com.dashcue.v1.CmdResult
import com.dashcue.v1.MediaAction
import com.dashcue.v1.MediaCmd
import kotlinx.coroutines.*
import java.util.concurrent.ConcurrentHashMap

class MediaCommandDispatcher(
    private val scope: CoroutineScope,
    private val audioManager: AudioManager? = null,
    private val actionExecutor: ((MediaAction, Long, MediaController) -> Unit)? = null
) {
    companion object {
        private const val TAG = "MediaCommandDispatcher"
        const val TIMEOUT_PLAY_PAUSE_MS = 1500L
        const val TIMEOUT_NEXT_PREV_MS = 3000L
        const val TIMEOUT_SEEK_MS = 1500L
        const val TIMEOUT_VOL_MS = 1000L

        const val EC_NONE = 0
        const val EC_UNSUPPORTED = 7
        const val EC_STALE = 11
        const val EC_NO_SESSION = 12
    }

    private data class PendingCommand(
        val commandId: Long,
        val action: MediaAction,
        val expectedStatePredicate: (PlaybackState?) -> Boolean,
        val callback: (CmdResult) -> Unit,
        val timeoutJob: Job
    )

    private val pendingCommands = ConcurrentHashMap<Long, PendingCommand>()

    /**
     * Dispatches an incoming MediaCmd from the HUD.
     */
    fun dispatchCommand(
        cmd: MediaCmd,
        controller: MediaController?,
        currentSnapshotVersion: Long,
        onResult: (CmdResult) -> Unit
    ) {
        val commandId = cmd.commandId.toLong() and 0xFFFFFFFFL

        if (controller == null) {
            onResult(
                CmdResult.newBuilder()
                    .setCommandId(cmd.commandId)
                    .setStatus(CmdResult.Status.ST_FAILED)
                    .setReason(EC_NO_SESSION)
                    .build()
            )
            return
        }

        // Validate snapshot version (§9.5, TC-AN-04)
        if (cmd.snapshotVersion.toLong() != currentSnapshotVersion) {
            DashCueLog.w(TAG, "Command $commandId rejected: stale snapshot version ${cmd.snapshotVersion} != $currentSnapshotVersion")
            onResult(
                CmdResult.newBuilder()
                    .setCommandId(cmd.commandId)
                    .setStatus(CmdResult.Status.ST_FAILED)
                    .setReason(EC_STALE)
                    .build()
            )
            return
        }

        val timeoutMs: Long
        val predicate: (PlaybackState?) -> Boolean

        when (cmd.action) {
            MediaAction.MA_PLAY -> {
                timeoutMs = TIMEOUT_PLAY_PAUSE_MS
                predicate = { state -> state?.state == PlaybackState.STATE_PLAYING }
                if (actionExecutor != null) {
                    actionExecutor.invoke(MediaAction.MA_PLAY, cmd.arg, controller)
                } else {
                    controller.transportControls.play() // Explicit play, never toggle (DEC-13)
                }
            }
            MediaAction.MA_PAUSE -> {
                timeoutMs = TIMEOUT_PLAY_PAUSE_MS
                predicate = { state -> state?.state == PlaybackState.STATE_PAUSED }
                if (actionExecutor != null) {
                    actionExecutor.invoke(MediaAction.MA_PAUSE, cmd.arg, controller)
                } else {
                    controller.transportControls.pause() // Explicit pause, never toggle (DEC-13)
                }
            }
            MediaAction.MA_NEXT -> {
                timeoutMs = TIMEOUT_NEXT_PREV_MS
                val initialPos = controller.playbackState?.position ?: -1L
                predicate = { state -> state != null && state.position != initialPos }
                if (actionExecutor != null) {
                    actionExecutor.invoke(MediaAction.MA_NEXT, cmd.arg, controller)
                } else {
                    controller.transportControls.skipToNext()
                }
            }
            MediaAction.MA_PREV -> {
                timeoutMs = TIMEOUT_NEXT_PREV_MS
                val initialPos = controller.playbackState?.position ?: -1L
                predicate = { state -> state != null && state.position != initialPos }
                if (actionExecutor != null) {
                    actionExecutor.invoke(MediaAction.MA_PREV, cmd.arg, controller)
                } else {
                    controller.transportControls.skipToPrevious()
                }
            }
            MediaAction.MA_SEEK_REL -> {
                timeoutMs = TIMEOUT_SEEK_MS
                val currentPos = controller.playbackState?.position ?: 0L
                val targetPos = maxOf(0L, currentPos + cmd.arg)
                predicate = { state -> state != null && Math.abs(state.position - targetPos) < 2000L }
                if (actionExecutor != null) {
                    actionExecutor.invoke(MediaAction.MA_SEEK_REL, cmd.arg, controller)
                } else {
                    controller.transportControls.seekTo(targetPos)
                }
            }
            MediaAction.MA_SEEK_ABS -> {
                timeoutMs = TIMEOUT_SEEK_MS
                val targetPos = maxOf(0L, cmd.arg)
                predicate = { state -> state != null && Math.abs(state.position - targetPos) < 2000L }
                if (actionExecutor != null) {
                    actionExecutor.invoke(MediaAction.MA_SEEK_ABS, cmd.arg, controller)
                } else {
                    controller.transportControls.seekTo(targetPos)
                }
            }
            MediaAction.MA_VOL_STEP -> {
                timeoutMs = TIMEOUT_VOL_MS
                val direction = if (cmd.arg > 0) AudioManager.ADJUST_RAISE else AudioManager.ADJUST_LOWER
                audioManager?.adjustStreamVolume(AudioManager.STREAM_MUSIC, direction, 0)
                predicate = { true }
            }
            MediaAction.MA_MUTE_SET -> {
                timeoutMs = TIMEOUT_VOL_MS
                val direction = if (cmd.arg != 0L) AudioManager.ADJUST_MUTE else AudioManager.ADJUST_UNMUTE
                audioManager?.adjustStreamVolume(AudioManager.STREAM_MUSIC, direction, 0)
                predicate = { true }
            }
            else -> {
                onResult(
                    CmdResult.newBuilder()
                        .setCommandId(cmd.commandId)
                        .setStatus(CmdResult.Status.ST_FAILED)
                        .setReason(EC_UNSUPPORTED)
                        .build()
                )
                return
            }
        }

        // Notify preliminary ST_ACCEPTED (phone invoked platform API)
        onResult(
            CmdResult.newBuilder()
                .setCommandId(cmd.commandId)
                .setStatus(CmdResult.Status.ST_ACCEPTED)
                .build()
        )

        // Set up timeout job
        val timeoutJob = scope.launch {
            delay(timeoutMs)
            val pending = pendingCommands.remove(commandId)
            if (pending != null) {
                DashCueLog.w(TAG, "Command $commandId timed out before observed state change")
                onResult(
                    CmdResult.newBuilder()
                        .setCommandId(cmd.commandId)
                        .setStatus(CmdResult.Status.ST_UNCONFIRMED)
                        .build()
                )
            }
        }

        pendingCommands[commandId] = PendingCommand(
            commandId = commandId,
            action = cmd.action,
            expectedStatePredicate = predicate,
            callback = onResult,
            timeoutJob = timeoutJob
        )
    }

    /**
     * Called whenever active MediaController reports an updated PlaybackState.
     */
    fun onObservedPlaybackState(state: PlaybackState?) {
        val nowMs = System.currentTimeMillis()
        val iterator = pendingCommands.entries.iterator()
        while (iterator.hasNext()) {
            val entry = iterator.next()
            val pending = entry.value
            if (pending.expectedStatePredicate(state)) {
                pending.timeoutJob.cancel()
                iterator.remove()
                DashCueLog.i(TAG, "Observed state change confirmed for command ${pending.commandId}")
                pending.callback(
                    CmdResult.newBuilder()
                        .setCommandId((pending.commandId and 0xFFFFFFFFL).toInt())
                        .setStatus(CmdResult.Status.ST_CONFIRMED)
                        .setObservedAtMs(nowMs)
                        .build()
                )
            }
        }
    }
}
