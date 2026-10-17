package com.dashcue.core.database

import org.junit.Assert.*
import org.junit.Test

class DatabaseTest {

    @Test
    fun testFavoriteDestinationModel() {
        val fav = FavoriteDestination(
            label = "Work",
            address = "123 Tech Park, Suite 400",
            latitude = 37.7749,
            longitude = -122.4194
        )
        assertEquals("Work", fav.label)
        assertEquals(37.7749, fav.latitude, 0.0001)
        assertEquals(-122.4194, fav.longitude, 0.0001)
    }

    @Test
    fun testRecentDestinationModel() {
        val recent = RecentDestination(
            label = "Airport",
            address = "Terminal 2",
            latitude = 37.6213,
            longitude = -122.3790
        )
        assertEquals("Airport", recent.label)
        assertTrue(recent.visitedAtMs > 0)
    }

    @Test
    fun testTripRecordModel() {
        val trip = TripRecord(
            routeId = 42L,
            destinationLabel = "Home",
            distanceM = 15420.0,
            durationS = 1260.0
        )
        assertEquals(42L, trip.routeId)
        assertEquals(15420.0, trip.distanceM, 0.1)
        assertEquals(1260.0, trip.durationS, 0.1)
    }

    @Test
    fun testUserSettingsDefaults() {
        val settings = UserSettings()
        assertEquals(100, settings.brightnessPct)
        assertEquals(0, settings.themeMode) // Auto
        assertEquals(10, settings.seekStepS)
        assertEquals(0, settings.units) // Metric
        assertFalse("Trip history must be default OFF per privacy rule", settings.tripHistoryEnabled)
        assertTrue(settings.drivingLockEnabled)
    }
}
