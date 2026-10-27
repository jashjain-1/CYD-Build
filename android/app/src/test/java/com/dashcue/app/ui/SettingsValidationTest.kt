package com.dashcue.app.ui

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SettingsValidationTest {

    @Test
    fun blankUrlIsValid() {
        assertTrue(isValidEndpointUrl(""))
        assertTrue(isValidEndpointUrl("   "))
    }

    @Test
    fun validHttpAndHttpsUrls() {
        assertTrue(isValidEndpointUrl("http://localhost:5000"))
        assertTrue(isValidEndpointUrl("https://router.project-osrm.org"))
        assertTrue(isValidEndpointUrl("https://nominatim.openstreetmap.org/search"))
    }

    @Test
    fun invalidUrlsRejected() {
        assertFalse(isValidEndpointUrl("ftp://example.com"))
        assertFalse(isValidEndpointUrl("invalid-url-string"))
        assertFalse(isValidEndpointUrl("http://"))
        assertFalse(isValidEndpointUrl("https://:8080"))
    }
}
