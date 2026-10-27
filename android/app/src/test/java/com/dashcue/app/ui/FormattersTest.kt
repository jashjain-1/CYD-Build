package com.dashcue.app.ui

import com.dashcue.core.ble.ReconnectionStateMachine
import com.dashcue.v1.MediaState
import com.dashcue.v1.PlaybackStatus
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FormattersTest {

    @Test
    fun distanceUsesMetresThenKilometres() {
        assertEquals("0 m", formatDistance(0))
        assertEquals("450 m", formatDistance(450))
        assertEquals("1.0 km", formatDistance(1000))
        assertEquals("2.1 km", formatDistance(2100))
    }

    @Test
    fun distanceUsesFeetAndMilesWhenImperial() {
        assertEquals("328 ft", formatDistance(100, imperial = true))
        assertEquals("1.0 mi", formatDistance(1610, imperial = true))
    }

    @Test
    fun clockNeverShowsNegativeTime() {
        assertEquals("--:--", formatClock(-1))
        assertEquals("00:00", formatClock(0))
        assertEquals("01:23", formatClock(83_000))
        assertEquals("61:01", formatClock(3_661_000))
    }

    @Test
    fun durationCollapsesHours() {
        assertEquals("0 min", formatDuration(0))
        assertEquals("18 min", formatDuration(18 * 60))
        assertEquals("1h 08m", formatDuration(68 * 60))
    }

    @Test
    fun playingPositionIsProjectedFromLastUpdate() {
        val state = MediaState.newBuilder()
            .setStatus(PlaybackStatus.PB_PLAYING)
            .setPositionMs(10_000)
            .setPositionUpdatedAtMs(1_000)
            .setPlaybackSpeed(1f)
            .setDurationMs(60_000)
            .build()

        assertEquals(12_500L, projectedPositionMs(state, 3_500))
    }

    @Test
    fun projectedPositionStopsAtDurationAndWhenPaused() {
        val playing = MediaState.newBuilder()
            .setStatus(PlaybackStatus.PB_PLAYING)
            .setPositionMs(59_000)
            .setPositionUpdatedAtMs(1_000)
            .setPlaybackSpeed(1f)
            .setDurationMs(60_000)
            .build()
        assertEquals(60_000L, projectedPositionMs(playing, 10_000))

        val paused = playing.toBuilder().setStatus(PlaybackStatus.PB_PAUSED).build()
        assertEquals(59_000L, projectedPositionMs(paused, 10_000))
    }

    @Test
    fun signalBarsMapRssiRanges() {
        assertEquals(4, signalBars(-50))
        assertEquals(3, signalBars(-65))
        assertEquals(2, signalBars(-75))
        assertEquals(1, signalBars(-85))
        assertEquals(0, signalBars(-100))
    }

    @Test
    fun connectionHealthOnlyForReady() {
        assertTrue(connectionIsHealthy(ReconnectionStateMachine.State.READY))
        assertFalse(connectionIsHealthy(ReconnectionStateMachine.State.NEGOTIATING))
        assertEquals("Connected", connectionLabel(ReconnectionStateMachine.State.READY))
    }

    @Test
    fun cardinalDirectionCoversEightSectors() {
        assertEquals("N", cardinalDirection(0f))
        assertEquals("N", cardinalDirection(10f))
        assertEquals("N", cardinalDirection(350f))
        assertEquals("NE", cardinalDirection(45f))
        assertEquals("E", cardinalDirection(90f))
        assertEquals("SE", cardinalDirection(135f))
        assertEquals("S", cardinalDirection(180f))
        assertEquals("SW", cardinalDirection(225f))
        assertEquals("W", cardinalDirection(270f))
        assertEquals("NW", cardinalDirection(315f))
        assertEquals("--", cardinalDirection(-1f))
        assertEquals("--", cardinalDirection(Float.NaN))
    }

    @Test
    fun formatHeadingProducesCleanString() {
        assertEquals("N 0°", formatHeading(0f))
        assertEquals("NW 315°", formatHeading(315f))
        assertEquals("E 90°", formatHeading(90f))
        assertEquals("--", formatHeading(-1f))
    }

    @Test
    fun formatSpeedHandlesMetricAndImperial() {
        assertEquals("0", formatSpeed(0, imperial = false))
        assertEquals("50", formatSpeed(500, imperial = false))
        assertEquals("100", formatSpeed(1004, imperial = false))
        assertEquals("31", formatSpeed(500, imperial = true)) // 50 km/h * 0.621371 = 31.06 -> 31 mph
        assertEquals("62", formatSpeed(1000, imperial = true))
        assertEquals("KM/H", speedUnit(imperial = false))
        assertEquals("MPH", speedUnit(imperial = true))
    }

    @Test
    fun formatSpeedLimitHandlesMetricAndImperial() {
        assertEquals("50", formatSpeedLimit(50, imperial = false))
        assertEquals("31", formatSpeedLimit(50, imperial = true))
        assertEquals("--", formatSpeedLimit(0, imperial = false))
        assertEquals("--", formatSpeedLimit(-1, imperial = false))
    }
}
