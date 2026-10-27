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

/** Cardinal direction from heading degrees (0° = N, 90° = E, 180° = S, 270° = W). */
fun cardinalDirection(headingDeg: Float): String {
    if (!headingDeg.isFinite() || headingDeg < 0f) return "--"
    var deg = headingDeg % 360f
    if (deg < 0f) deg += 360f
    val directions = arrayOf("N", "NE", "E", "SE", "S", "SW", "W", "NW")
    val idx = (((deg + 22.5f) / 45.0f).toInt()) % 8
    return directions[idx]
}

/** Formatted digital compass heading with cardinal and degrees, e.g. "NW 315°". */
fun formatHeading(headingDeg: Float): String {
    if (!headingDeg.isFinite() || headingDeg < 0f) return "--"
    val cardinal = cardinalDirection(headingDeg)
    var deg = (headingDeg + 0.5f).toInt() % 360
    if (deg < 0) deg += 360
    return "$cardinal $deg°"
}

/** Digital speed readout converted to display units. */
fun formatSpeed(speedKmhX10: Int, imperial: Boolean = false): String {
    val kmh = speedKmhX10 / 10.0
    return if (imperial) {
        val mph = kmh * 0.621371
        mph.roundToInt().toString()
    } else {
        kmh.roundToInt().toString()
    }
}

/** Unit label for speed. */
fun speedUnit(imperial: Boolean = false): String = if (imperial) "MPH" else "KM/H"

/** Speed limit badge string. */
fun formatSpeedLimit(limitKmh: Int, imperial: Boolean = false): String {
    if (limitKmh <= 0) return "--"
    return if (imperial) {
        (limitKmh * 0.621371).roundToInt().toString()
    } else {
        limitKmh.toString()
    }
}
