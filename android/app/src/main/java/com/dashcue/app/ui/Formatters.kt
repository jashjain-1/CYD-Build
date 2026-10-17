package com.dashcue.app.ui

import com.dashcue.core.ble.ReconnectionStateMachine
import com.dashcue.v1.MediaState
import com.dashcue.v1.PlaybackStatus
import kotlin.math.roundToInt

/** Distance with a unit system. Values are metres. */
fun formatDistance(meters: Int, imperial: Boolean = false): String {
    val m = meters.coerceAtLeast(0)
    if (imperial) {
        val feet = (m * 3.28084).roundToInt()
        return if (feet >= 5280) {
            val miles = feet / 5280.0
            String.format("%.1f mi", miles)
        } else {
            "$feet ft"
        }
    }
    return if (m >= 1000) String.format("%.1f km", m / 1000.0) else "$m m"
}

/** mm:ss for a media position or duration. */
fun formatClock(millis: Long): String {
    if (millis < 0) return "--:--"
    val totalSeconds = millis / 1000
    val minutes = totalSeconds / 60
    val seconds = totalSeconds % 60
    return String.format("%02d:%02d", minutes, seconds)
}

/** Human duration, e.g. 18 min / 1 h 08 min. */
fun formatDuration(seconds: Int): String {
    val s = seconds.coerceAtLeast(0)
    val minutes = s / 60
    return if (minutes >= 60) {
        String.format("%dh %02dm", minutes / 60, minutes % 60)
    } else {
        "$minutes min"
    }
}

/** Playback position projected from the last reported state. */
fun projectedPositionMs(state: MediaState?, nowElapsedMs: Long): Long {
    if (state == null) return 0L
    val base = state.positionMs
    if (base < 0) return 0L
    if (state.status != PlaybackStatus.PB_PLAYING) return base
    val updated = state.positionUpdatedAtMs
    if (updated <= 0 || nowElapsedMs < updated) return base
    val delta = (nowElapsedMs - updated).coerceAtLeast(0L)
    val projected = base + (delta * state.playbackSpeed.coerceAtLeast(0f)).toLong()
    val duration = state.durationMs
    return if (duration > 0) projected.coerceAtMost(duration) else projected
}

/** Compact signal quality for the diagnostics/dashboard pills. */
fun signalBars(rssiDbm: Int): Int = when {
    rssiDbm >= -55 -> 4
    rssiDbm >= -67 -> 3
    rssiDbm >= -78 -> 2
    rssiDbm >= -90 -> 1
    else -> 0
}

fun connectionLabel(state: ReconnectionStateMachine.State): String = when (state) {
    ReconnectionStateMachine.State.DISCONNECTED -> "Disconnected"
    ReconnectionStateMachine.State.SCANNING -> "Scanning"
    ReconnectionStateMachine.State.CONNECTING -> "Connecting"
    ReconnectionStateMachine.State.DISCOVERING_SERVICES -> "Discovering"
    ReconnectionStateMachine.State.NEGOTIATING -> "Negotiating"
    ReconnectionStateMachine.State.SYNCHRONISING -> "Synchronising"
    ReconnectionStateMachine.State.READY -> "Connected"
    ReconnectionStateMachine.State.TERMINAL_ERROR -> "Error"
}

fun connectionIsHealthy(state: ReconnectionStateMachine.State): Boolean =
    state == ReconnectionStateMachine.State.READY

/** Remaining-time label that never claims more precision than it has. */
fun remainingLabel(seconds: Int): String = "${formatDuration(seconds)} left"
