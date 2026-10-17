package com.dashcue.feature.media

import android.media.session.PlaybackState
import com.dashcue.v1.MediaAction
import com.dashcue.v1.PlaybackStatus

object MediaActionMapper {

    /**
     * Maps Android PlaybackState actions bitmask to DashCue protobuf MediaAction capability bitmask (§10.2).
     */
    fun mapCapabilities(actions: Long): Int {
        var capabilities = 0

        if ((actions and PlaybackState.ACTION_PLAY) != 0L) {
            capabilities = capabilities or (1 shl MediaAction.MA_PLAY_VALUE)
        }
        if ((actions and PlaybackState.ACTION_PAUSE) != 0L) {
            capabilities = capabilities or (1 shl MediaAction.MA_PAUSE_VALUE)
        }
        if ((actions and PlaybackState.ACTION_PLAY_PAUSE) != 0L) {
            capabilities = capabilities or (1 shl MediaAction.MA_PLAY_VALUE)
            capabilities = capabilities or (1 shl MediaAction.MA_PAUSE_VALUE)
        }
        if ((actions and PlaybackState.ACTION_SKIP_TO_NEXT) != 0L) {
            capabilities = capabilities or (1 shl MediaAction.MA_NEXT_VALUE)
        }
        if ((actions and PlaybackState.ACTION_SKIP_TO_PREVIOUS) != 0L) {
            capabilities = capabilities or (1 shl MediaAction.MA_PREV_VALUE)
        }
        if ((actions and PlaybackState.ACTION_SEEK_TO) != 0L) {
            capabilities = capabilities or (1 shl MediaAction.MA_SEEK_REL_VALUE)
            capabilities = capabilities or (1 shl MediaAction.MA_SEEK_ABS_VALUE)
        }
        // Always support volume stepping & mute control via system audio stream
        capabilities = capabilities or (1 shl MediaAction.MA_VOL_STEP_VALUE)
        capabilities = capabilities or (1 shl MediaAction.MA_VOL_SET_VALUE)
        capabilities = capabilities or (1 shl MediaAction.MA_MUTE_SET_VALUE)

        return capabilities
    }

    /**
     * Maps Android PlaybackState integer state code to DashCue PlaybackStatus enum.
     */
    fun mapPlaybackStatus(state: Int): PlaybackStatus {
        return when (state) {
            PlaybackState.STATE_PLAYING -> PlaybackStatus.PB_PLAYING
            PlaybackState.STATE_PAUSED -> PlaybackStatus.PB_PAUSED
            PlaybackState.STATE_BUFFERING,
            PlaybackState.STATE_CONNECTING -> PlaybackStatus.PB_BUFFERING
            PlaybackState.STATE_STOPPED -> PlaybackStatus.PB_STOPPED
            PlaybackState.STATE_NONE -> PlaybackStatus.PB_NO_SESSION
            else -> PlaybackStatus.PB_UNKNOWN
        }
    }
}
