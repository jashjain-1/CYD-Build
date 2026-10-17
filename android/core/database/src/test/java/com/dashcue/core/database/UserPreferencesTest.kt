package com.dashcue.core.database

import org.junit.Assert.*
import org.junit.Test

class UserPreferencesTest {

    @Test
    fun testPrivacyByDefaultTripHistoryIsOff() {
        // Privacy-by-Default (Rule 6 / DEC-20 / TC-AN-12): trip history default OFF
        val prefs = UserPreferences()
        assertFalse("tripHistoryEnabled must be FALSE by default for privacy", prefs.tripHistoryEnabled)
    }

    @Test
    fun testDefaultSettingsValues() {
        val prefs = UserPreferences()
        assertEquals(80, prefs.brightnessPct)
        assertEquals(0, prefs.theme)
        assertEquals(0, prefs.orientation)
        assertEquals(0, prefs.units)
        assertEquals(10, prefs.seekStepSeconds)
        assertEquals("", prefs.osrmEndpoint)
        assertEquals("", prefs.nominatimEndpoint)
    }
}
