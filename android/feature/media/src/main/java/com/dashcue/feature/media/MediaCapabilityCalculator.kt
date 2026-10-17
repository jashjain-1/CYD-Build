package com.dashcue.feature.media

import android.media.session.PlaybackState
import com.dashcue.v1.MediaAction

/**
 * Derives DashCue MediaState.capabilities bitmask strictly from Android PlaybackState actions (§10.1, TC-AN-04).
 * Bit (1 << MediaAction) is set when supported.
 */
object MediaCapabilityCalculator {

    fun calculateCapabilities(playbackStateActions: Long, supportsVolume: Boolean = true): Int {
        var caps = 0

        if ((playbackStateActions and PlaybackState.ACTION_PLAY) != 0L) {
            caps = caps or (1 shl MediaAction.MA_PLAY.number)
        }
        if ((playbackStateActions and PlaybackState.ACTION_PAUSE) != 0L) {
            caps = caps or (1 shl MediaAction.MA_PAUSE.number)
        }
        if ((playbackStateActions and PlaybackState.ACTION_SKIP_TO_NEXT) != 0L) {
            caps = caps or (1 shl MediaAction.MA_NEXT.number)
        }
        if ((playbackStateActions and PlaybackState.ACTION_SKIP_TO_PREVIOUS) != 0L) {
            caps = caps or (1 shl MediaAction.MA_PREV.number)
        }
        if ((playbackStateActions and PlaybackState.ACTION_SEEK_TO) != 0L) {
            caps = caps or (1 shl MediaAction.MA_SEEK_REL.number)
            caps = caps or (1 shl MediaAction.MA_SEEK_ABS.number)
        }

        if (supportsVolume) {
            caps = caps or (1 shl MediaAction.MA_VOL_STEP.number)
            caps = caps or (1 shl MediaAction.MA_VOL_SET.number)
            caps = caps or (1 shl MediaAction.MA_MUTE_SET.number)
        }

        return caps
    }

    fun isActionSupported(capabilities: Int, action: MediaAction): Boolean {
        return (capabilities and (1 shl action.number)) != 0
    }
}
