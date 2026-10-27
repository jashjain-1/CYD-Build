package com.dashcue.app

import com.dashcue.feature.media.DashCueNotificationListenerService
import com.dashcue.v1.NavState
import com.dashcue.v1.NavStatus
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class StandbyTelemetryTest {

    @Test
    fun standbyNavStateMaintainsRealTelemetry() {
        val speedKmhX10 = 550 // 55.0 km/h
        val headingDeg = 285.4f
        val street = "Sunset Boulevard"
        val limitKmh = 50

        val standbyNav = NavState.newBuilder()
            .setStatus(NavStatus.NAV_NONE)
            .setSpeedKmhX10(speedKmhX10)
            .setHeadingDeg(headingDeg)
            .setGpsAccuracyM(4.2f)
            .setStreet(street)
            .setSpeedLimitKmh(limitKmh)
            .setPhoneTsMs(System.currentTimeMillis())
            .build()

        assertEquals(NavStatus.NAV_NONE, standbyNav.status)
        assertEquals(550, standbyNav.speedKmhX10)
        assertEquals(285.4f, standbyNav.headingDeg, 0.01f)
        assertEquals("Sunset Boulevard", standbyNav.street)
        assertEquals(50, standbyNav.speedLimitKmh)

        // Verify overspeed detection logic
        val speedInt = standbyNav.speedKmhX10 / 10
        val isOverspeed = standbyNav.speedLimitKmh > 0 && speedInt > standbyNav.speedLimitKmh
        assertTrue(isOverspeed)
    }

    @Test
    fun standbyNavWithinSpeedLimitIsNotOverspeed() {
        val standbyNav = NavState.newBuilder()
            .setStatus(NavStatus.NAV_NONE)
            .setSpeedKmhX10(450) // 45 km/h
            .setSpeedLimitKmh(50)
            .build()

        val speedInt = standbyNav.speedKmhX10 / 10
        val isOverspeed = standbyNav.speedLimitKmh > 0 && speedInt > standbyNav.speedLimitKmh
        assertFalse(isOverspeed)
    }

    @Test
    fun dominantAccentRgbDefaultsToNull() = runTest {
        val flow = DashCueNotificationListenerService.dominantAccentRgb
        assertNull(flow.value)
    }
}
