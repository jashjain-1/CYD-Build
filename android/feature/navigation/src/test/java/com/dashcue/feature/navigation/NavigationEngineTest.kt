package com.dashcue.feature.navigation

import android.location.Location
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
@OptIn(ExperimentalCoroutinesApi::class)
class NavigationEngineTest {

    @Test
    fun testDrivingLockEngagedAtSpeed8KmhOrAbove() = runTest {
        var reportedSpeed = -1
        val engine = NavigationEngine(
            scope = this,
            onSpeedUpdated = { reportedSpeed = it }
        )

        // 8.0 km/h is 8.0 / 3.6 = 2.2222 m/s
        val loc = Location("gps").apply {
            latitude = 37.7749
            longitude = -122.4194
            speed = 2.223f // 2.223 * 3.6 * 10 = 80.028 -> 80 (8.0 km/h)
        }

        engine.onNewLocation(loc)
        assertTrue("Driving lock should be engaged at speed >= 8.0 km/h", engine.drivingLockActive.value)
        assertEquals(80, reportedSpeed)

        // 36 km/h is 10 m/s
        val locFast = Location("gps").apply {
            latitude = 37.7749
            longitude = -122.4194
            speed = 10.0f // 10.0 * 3.6 * 10 = 360
        }
        engine.onNewLocation(locFast)
        assertTrue("Driving lock should remain engaged at 36 km/h", engine.drivingLockActive.value)
        assertEquals(360, reportedSpeed)
    }

    @Test
    fun testDrivingLockDisengagedBelow8Kmh() = runTest {
        var reportedSpeed = -1
        val engine = NavigationEngine(
            scope = this,
            onSpeedUpdated = { reportedSpeed = it }
        )

        // 7.2 km/h is 2.0 m/s -> 2.0 * 3.6 * 10 = 72
        val locSlow = Location("gps").apply {
            latitude = 37.7749
            longitude = -122.4194
            speed = 2.0f
        }

        engine.onNewLocation(locSlow)
        assertFalse("Driving lock should NOT be engaged below 8.0 km/h", engine.drivingLockActive.value)
        assertEquals(72, reportedSpeed)

        // Stationary (0.0 m/s)
        val locStationary = Location("gps").apply {
            latitude = 37.7749
            longitude = -122.4194
            speed = 0.0f
        }
        engine.onNewLocation(locStationary)
        assertFalse(engine.drivingLockActive.value)
        assertEquals(0, reportedSpeed)
    }

    @Test
    fun testDrivingLockWithUnknownSpeed() = runTest {
        var reportedSpeed = 0
        val engine = NavigationEngine(
            scope = this,
            onSpeedUpdated = { reportedSpeed = it }
        )

        // Location without speed
        val locNoSpeed = Location("gps").apply {
            latitude = 37.7749
            longitude = -122.4194
            // speed is not set, hasSpeed() returns false
        }

        engine.onNewLocation(locNoSpeed)
        assertFalse("Driving lock should NOT be engaged when speed is unknown", engine.drivingLockActive.value)
        assertEquals(-1, reportedSpeed)
    }

    @Test
    fun testStopNavigationResetsDrivingLock() = runTest {
        val engine = NavigationEngine(scope = this)
        val locFast = Location("gps").apply {
            latitude = 37.7749
            longitude = -122.4194
            speed = 10.0f
        }
        engine.onNewLocation(locFast)
        assertTrue(engine.drivingLockActive.value)

        engine.stopNavigation()
        assertFalse("stopNavigation() should disengage driving lock", engine.drivingLockActive.value)
    }

    @Test
    fun testDistanceToRouteComputation() = runTest {
        val engine = NavigationEngine(scope = this)
        val points = listOf(
            GeoPoint(37.0, -122.0),
            GeoPoint(37.0, -122.01),
            GeoPoint(37.0, -122.02)
        )

        // Point directly on segment 0
        val userOnRoute = GeoPoint(37.0, -122.005)
        val (dist, segmentIdx, offset) = engine.computeDistanceToRoute(userOnRoute, points)

        assertEquals(0, segmentIdx)
        assertTrue("Distance should be very close to 0", dist < 1.0)
        assertTrue("Offset along segment should be ~444m", offset > 400.0 && offset < 500.0)
    }
}
