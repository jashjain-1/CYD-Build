package com.dashcue.feature.media

import com.dashcue.v1.CmdResult
import com.dashcue.v1.ErrorCode
import com.dashcue.v1.MediaAction
import com.dashcue.v1.MediaCmd
import com.dashcue.v1.PlaybackStatus
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow

/**
 * Observed confirmation protocol engine for media transport controls (§10.1, DEC-13, TC-AN-05).
 *
 * Rules:
 * - Reject commands with stale snapshot_version immediately (ST_FAILED, EC_STALE).
 * - Reject unsupported commands (ST_FAILED, EC_UNSUPPORTED).
 * - Return ST_ACCEPTED upon platform API invocation.
 * - Mark command ST_CONFIRMED ONLY when the platform callback observes the state change before timeout.
 * - Return ST_UNCONFIRMED if timeout expires without observed change.
 */
class MediaConfirmationEngine(
    private val scope: CoroutineScope
) {
    companion object {
        const val TIMEOUT_PLAY_PAUSE_MS = 1500L
        const val TIMEOUT_NEXT_PREV_MS = 3000L
        const val TIMEOUT_SEEK_MS = 1500L
        const val TIMEOUT_VOLUME_MS = 1000L

        fun getTimeoutMs(action: MediaAction): Long = when (action) {
            MediaAction.MA_PLAY, MediaAction.MA_PAUSE -> TIMEOUT_PLAY_PAUSE_MS
            MediaAction.MA_NEXT, MediaAction.MA_PREV -> TIMEOUT_NEXT_PREV_MS
            MediaAction.MA_SEEK_REL, MediaAction.MA_SEEK_ABS -> TIMEOUT_SEEK_MS
            MediaAction.MA_VOL_STEP, MediaAction.MA_VOL_SET, MediaAction.MA_MUTE_SET -> TIMEOUT_VOLUME_MS
            else -> 1500L
        }
    }

    private val _cmdResults = MutableSharedFlow<CmdResult>(replay = 16, extraBufferCapacity = 16)
    val cmdResults: SharedFlow<CmdResult> = _cmdResults.asSharedFlow()

    data class PendingCommand(
        val cmd: MediaCmd,
        val submittedAtMs: Long,
        val timeoutJob: Job
    )

    private val pendingCommands = mutableMapOf<Long, PendingCommand>()

    /**
     * Submits a media command from the HUD.
     * Returns whether the command was accepted for execution.
     */
    @Synchronized
    fun submitCommand(
        cmd: MediaCmd,
        currentSnapshotVersion: Long,
        capabilities: Int,
        currentTimeMs: Long = System.currentTimeMillis(),
        executeAction: (MediaAction, Long) -> Boolean
    ): Boolean {
        // 1. Stale check
        if (cmd.snapshotVersion.toLong() != currentSnapshotVersion) {
            _cmdResults.tryEmit(
                CmdResult.newBuilder()
                    .setCommandId(cmd.commandId)
                    .setStatus(CmdResult.Status.ST_FAILED)
                    .setReason(ErrorCode.EC_STALE_VALUE)
                    .setObservedAtMs(currentTimeMs)
                    .build()
            )
            return false
        }

        // 2. Capabilities check
        if (!MediaCapabilityCalculator.isActionSupported(capabilities, cmd.action)) {
            _cmdResults.tryEmit(
                CmdResult.newBuilder()
                    .setCommandId(cmd.commandId)
                    .setStatus(CmdResult.Status.ST_FAILED)
                    .setReason(ErrorCode.EC_UNSUPPORTED_VALUE)
                    .setObservedAtMs(currentTimeMs)
                    .build()
            )
            return false
        }

        // 3. Emit ACCEPTED
        _cmdResults.tryEmit(
            CmdResult.newBuilder()
                .setCommandId(cmd.commandId)
                .setStatus(CmdResult.Status.ST_ACCEPTED)
                .setObservedAtMs(currentTimeMs)
                .build()
        )

        // 4. Invoke platform action
        val success = executeAction(cmd.action, cmd.arg)
        if (!success) {
            _cmdResults.tryEmit(
                CmdResult.newBuilder()
                    .setCommandId(cmd.commandId)
                    .setStatus(CmdResult.Status.ST_FAILED)
                    .setReason(ErrorCode.EC_INTERNAL_VALUE)
                    .setObservedAtMs(currentTimeMs)
                    .build()
            )
            return false
        }

        // 5. Schedule timeout for confirmation
        val timeoutMs = getTimeoutMs(cmd.action)
        val job = scope.launch {
            delay(timeoutMs)
            onCommandTimeout(cmd.commandId.toLong())
        }

        pendingCommands[cmd.commandId.toLong()] = PendingCommand(cmd, currentTimeMs, job)
        return true
    }

    /**
     * Called when MediaController.Callback observes a real playback state or track change.
     */
    /**
     * Called when MediaController.Callback observes metadata/track change.
     */
    @Synchronized
    fun onObservedTrackChange(observedAtMs: Long = System.currentTimeMillis()) {
        val iterator = pendingCommands.entries.iterator()
        while (iterator.hasNext()) {
            val entry = iterator.next()
            val pending = entry.value
            if (pending.cmd.action == MediaAction.MA_NEXT || pending.cmd.action == MediaAction.MA_PREV) {
                pending.timeoutJob.cancel()
                iterator.remove()
                _cmdResults.tryEmit(
                    CmdResult.newBuilder()
                        .setCommandId(pending.cmd.commandId)
                        .setStatus(CmdResult.Status.ST_CONFIRMED)
                        .setReason(ErrorCode.EC_NONE_VALUE)
                        .setObservedAtMs(observedAtMs)
                        .build()
                )
            }
        }
    }

    @Synchronized
    fun onObservedStateChange(
        observedStatus: PlaybackStatus,
        observedAtMs: Long = System.currentTimeMillis()
    ) {
        val iterator = pendingCommands.entries.iterator()
        while (iterator.hasNext()) {
            val entry = iterator.next()
            val pending = entry.value
            var matches = false

            when (pending.cmd.action) {
                MediaAction.MA_PLAY -> {
                    if (observedStatus == PlaybackStatus.PB_PLAYING) matches = true
                }
                MediaAction.MA_PAUSE -> {
                    if (observedStatus == PlaybackStatus.PB_PAUSED || observedStatus == PlaybackStatus.PB_STOPPED) matches = true
                }
                MediaAction.MA_NEXT, MediaAction.MA_PREV, MediaAction.MA_SEEK_REL, MediaAction.MA_SEEK_ABS,
                MediaAction.MA_VOL_STEP, MediaAction.MA_VOL_SET -> {
                    // Any state update confirms the action
                    matches = true
                }
                else -> {}
            }

            if (matches) {
                pending.timeoutJob.cancel()
                iterator.remove()
                _cmdResults.tryEmit(
                    CmdResult.newBuilder()
                        .setCommandId(pending.cmd.commandId)
                        .setStatus(CmdResult.Status.ST_CONFIRMED)
                        .setReason(ErrorCode.EC_NONE_VALUE)
                        .setObservedAtMs(observedAtMs)
                        .build()
                )
            }
        }
    }

    @Synchronized
    private fun onCommandTimeout(commandId: Long) {
        val pending = pendingCommands.remove(commandId) ?: return
        _cmdResults.tryEmit(
            CmdResult.newBuilder()
                .setCommandId(pending.cmd.commandId)
                .setStatus(CmdResult.Status.ST_UNCONFIRMED)
                .setObservedAtMs(System.currentTimeMillis())
                .build()
        )
    }

    @Synchronized
    fun getPendingCount(): Int = pendingCommands.size
}
